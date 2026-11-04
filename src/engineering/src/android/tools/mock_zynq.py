"""Local NDJSON v1 control, 48 kHz PCM, and WAV download simulator."""

import argparse
import io
import json
import math
import socketserver
import struct
import threading
import time
import wave
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import unquote, urlsplit

MAX_CONTROL_BYTES = 2048


def gained_sample(sample, gain_db):
    amplified = round(sample * 10 ** (gain_db / 20))
    return max(-32768, min(32767, amplified))


def make_wav(seconds, gain_db=0):
    """Small, valid 48 kHz mono 16-bit WAV fixture."""
    output = io.BytesIO()
    with wave.open(output, "wb") as writer:
        writer.setnchannels(1)
        writer.setsampwidth(2)
        writer.setframerate(48000)
        for start in range(0, round(seconds * 48000), 480):
            count = min(480, round(seconds * 48000) - start)
            samples = [gained_sample(round(7000 * math.sin(2 * math.pi * 440 * (start + i) / 48000)), gain_db)
                       for i in range(count)]
            writer.writeframesraw(struct.pack(f"<{count}h", *samples))
    return output.getvalue()


class MockState:
    def __init__(self, audio_port=9000, drop_audio_once_after_blocks=0,
                 truncate_download_once=False, malformed_spectrum_once=False,
                 http_chunk_delay_ms=0, gain_supported=True, reject_gain_once=False):
        self.lock = threading.Lock()
        self.started = time.monotonic()
        self.target_angle = 90
        self.recording = False
        self.recording_since = None
        self.recording_index = 0
        self.recordings = {"demo": ("demo_440hz.wav", make_wav(2))}
        self.truncate_download_once = truncate_download_once
        self.malformed_spectrum_once = malformed_spectrum_once
        self.http_chunk_delay_ms = http_chunk_delay_ms
        self.streaming = False
        self.gain_db = 0
        self.gain_supported = gain_supported
        self.reject_gain_once = reject_gain_once
        self.audio_port = audio_port
        self.drop_audio_once_after_blocks = drop_audio_once_after_blocks

    def sources(self):
        phase = time.monotonic() - self.started
        return [
            {"source_id": 0, "angle_deg": round(48 + 9 * math.sin(phase * 0.45)) % 360, "strength": 850},
            {"source_id": 1, "angle_deg": round(221 + 12 * math.sin(phase * 0.28 + 1.2)) % 360, "strength": 620},
        ]

    def spectrum(self):
        sources = self.sources()
        values = []
        for index in range(72):
            angle = index * 5
            energy = 85
            for source, amplitude, width in zip(sources, (760, 510), (520, 780)):
                distance = abs(angle - source["angle_deg"])
                distance = min(distance, 360 - distance)
                energy += amplitude * math.exp(-distance * distance / width)
            values.append(max(0, min(1000, round(energy))))
        return values


class MockServer(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True

    def __init__(self, address, state=None):
        super().__init__(address, ControlHandler)
        self.state = state or MockState()


class AudioServer(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True

    def __init__(self, address, state):
        super().__init__(address, AudioHandler)
        self.state = state
        state.audio_port = self.server_address[1]


class AudioHandler(socketserver.BaseRequestHandler):
    def handle(self):
        state = self.server.state
        sample_index = 0
        blocks_sent = 0
        deadline = time.monotonic()
        while True:
            with state.lock:
                if not state.streaming:
                    return
                gain_db = state.gain_db
                if state.drop_audio_once_after_blocks and blocks_sent >= state.drop_audio_once_after_blocks:
                    state.drop_audio_once_after_blocks = 0
                    return
            # 480 samples = 10 ms at 48 kHz, 960 raw little-endian PCM bytes.
            samples = [gained_sample(round(9000 * math.sin(2 * math.pi * 440 * (sample_index + i) / 48000)), gain_db)
                       for i in range(480)]
            sample_index += 480
            try:
                self.request.sendall(struct.pack("<480h", *samples))
            except (BrokenPipeError, ConnectionResetError, OSError):
                return
            blocks_sent += 1
            deadline += 0.01
            time.sleep(max(0, deadline - time.monotonic()))


class WavHttpServer(ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self, address, state):
        super().__init__(address, WavHttpHandler)
        self.state = state


class WavHttpHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        path = urlsplit(self.path).path
        prefix = "/api/recordings/"
        if not path.startswith(prefix) or not path[len(prefix):] or "/" in path[len(prefix):]:
            self.send_error(404)
            return
        recording_id = unquote(path[len(prefix):])
        with self.server.state.lock:
            recording = self.server.state.recordings.get(recording_id)
            truncate = self.server.state.truncate_download_once
            if truncate and recording:
                self.server.state.truncate_download_once = False
        if recording is None:
            self.send_error(404)
            return
        payload = recording[1]
        self.send_response(200)
        self.send_header("Content-Type", "audio/wav")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        try:
            body = payload[:len(payload) // 2] if truncate else payload
            for offset in range(0, len(body), 4096):
                self.wfile.write(body[offset:offset + 4096])
                if self.server.state.http_chunk_delay_ms:
                    self.wfile.flush()
                    time.sleep(self.server.state.http_chunk_delay_ms / 1000)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def log_message(self, format, *args):
        pass


class ControlHandler(socketserver.StreamRequestHandler):
    def handle(self):
        try:
            while True:
                line = self.rfile.readline(MAX_CONTROL_BYTES + 2)
                if not line:
                    return
                if len(line) > MAX_CONTROL_BYTES or not line.endswith(b"\n"):
                    self.send_error(0, "FRAME_TOO_LARGE", "Control message exceeds 2048 bytes")
                    return
                try:
                    request = json.loads(line.decode("utf-8"))
                    if not isinstance(request, dict):
                        raise ValueError("Request must be a JSON object")
                    request_id = request.get("id")
                    if type(request_id) is not int or request_id <= 0:
                        raise ValueError("id must be a positive integer")
                    if type(request.get("v")) is not int or request.get("v") != 1:
                        self.send_error(request_id, "UNSUPPORTED_VERSION", "Protocol version must be 1")
                        continue
                    command = request.get("cmd")
                    args = request.get("args")
                    if not isinstance(args, dict):
                        self.send_error(request_id, "INVALID_ARGUMENT", "args must be an object")
                        continue
                    self.dispatch(request_id, command, args)
                except (UnicodeError, json.JSONDecodeError, ValueError) as exc:
                    self.send_error(0, "BAD_REQUEST", str(exc))
                except (BrokenPipeError, ConnectionResetError):
                    return
        finally:
            with self.server.state.lock:
                self.server.state.streaming = False

    def send(self, payload):
        message = (json.dumps(payload, ensure_ascii=False, separators=(",", ":")) + "\n").encode("utf-8")
        if len(message) > MAX_CONTROL_BYTES:
            raise ValueError("Simulator response exceeds 2048 bytes")
        self.wfile.write(message)

    def success(self, request_id, data):
        self.send({"v": 1, "id": request_id, "ok": True, "data": data})

    def send_error(self, request_id, code, message):
        self.send({"v": 1, "id": request_id, "ok": False, "error": {"code": code, "message": message}})

    def dispatch(self, request_id, command, args):
        state = self.server.state
        if command == "PING":
            self.success(request_id, {"uptime_ms": round((time.monotonic() - state.started) * 1000)})
        elif command == "GET_INFO":
            self.success(request_id, {
                "device_name": "Windows Zynq Simulator",
                "firmware_version": "mock-1.0",
                "protocol_version": 1,
                "sample_rate_hz": 48000,
                "simulated": True,
                "capabilities": {"stream_audio": True, "file_download": True,
                                 "gain_control": state.gain_supported},
            })
        elif command == "GET_FILTER_STATUS" and state.gain_supported:
            with state.lock:
                gain_db = state.gain_db
            print(f"GET_FILTER_STATUS id={request_id} gain_db={gain_db}", flush=True)
            self.success(request_id, {"gain_db": gain_db})
        elif command == "SET_FILTER_GAIN" and state.gain_supported:
            gain_db = args.get("gain_db")
            print(f"SET_FILTER_GAIN id={request_id} gain_db={gain_db}", flush=True)
            if type(gain_db) is not int or gain_db not in range(97):
                self.send_error(request_id, "INVALID_ARGUMENT", "gain_db must be an integer from 0 to 96")
                return
            with state.lock:
                reject = state.reject_gain_once
                state.reject_gain_once = False
                if not reject:
                    state.gain_db = gain_db
            if reject:
                self.send_error(request_id, "GAIN_REJECTED", "Simulator rejected this gain setting")
            else:
                self.success(request_id, {"gain_db": gain_db})
        elif command == "GET_STATUS":
            with state.lock:
                data = {"target_angle_deg": state.target_angle, "recording": state.recording,
                        "streaming": state.streaming, "simulated": True}
            self.success(request_id, data)
        elif command == "GET_SPECTRUM":
            with state.lock:
                malformed = state.malformed_spectrum_once
                state.malformed_spectrum_once = False
            if malformed:
                self.wfile.write(b'{"v":1,"id":' + str(request_id).encode() + b',"ok":true,"data":\n')
            else:
                self.success(request_id, {"step_deg": 5, "values": state.spectrum(), "simulated": True})
        elif command == "GET_SOURCES":
            self.success(request_id, {"sources": state.sources(), "simulated": True})
        elif command == "SET_ANGLE":
            angle = args.get("angle_deg")
            if type(angle) is not int or angle not in range(360):
                self.send_error(request_id, "INVALID_ARGUMENT", "angle_deg must be 0..359")
                return
            with state.lock:
                state.target_angle = angle
            self.success(request_id, {"angle_deg": angle})
        elif command in ("START_RECORD", "STOP_RECORD"):
            with state.lock:
                if command == "START_RECORD" and not state.recording:
                    state.recording_since = time.monotonic()
                    state.recording = True
                elif command == "STOP_RECORD" and state.recording:
                    duration = max(1, min(10, time.monotonic() - state.recording_since))
                    state.recording_index += 1
                    recording_id = f"mock-{state.recording_index:03d}"
                    state.recordings[recording_id] = (f"recording_{state.recording_index:03d}.wav", make_wav(duration, state.gain_db))
                    state.recording = False
                    state.recording_since = None
                recording = state.recording
            self.success(request_id, {"recording": recording, "simulated": True})
        elif command == "LIST_RECORDINGS":
            with state.lock:
                recordings = [{"id": key, "filename": name, "size_bytes": len(payload)}
                              for key, (name, payload) in state.recordings.items()]
            self.success(request_id, {"recordings": recordings, "simulated": True})
        elif command == "START_STREAM":
            with state.lock:
                state.streaming = True
                port = state.audio_port
            self.success(request_id, {"sample_rate_hz": 48000, "channels": 1,
                                      "bits_per_sample": 16, "encoding": "S16LE", "port": port})
        elif command == "STOP_STREAM":
            with state.lock:
                state.streaming = False
            self.success(request_id, {"streaming": False})
        else:
            self.send_error(request_id, "UNKNOWN_COMMAND", "Unknown command")


def main():
    parser = argparse.ArgumentParser(description="Zynq NDJSON control simulator")
    parser.add_argument("--host", default="0.0.0.0", help="Bind address (default: 0.0.0.0)")
    parser.add_argument("--port", type=int, default=7000, help="TCP control port (default: 7000)")
    parser.add_argument("--audio-port", type=int, default=9000, help="Raw PCM port (default: 9000)")
    parser.add_argument("--http-port", type=int, default=8000, help="WAV HTTP port (default: 8000)")
    parser.add_argument("--truncate-download-once", action="store_true",
                        help="Send only half of the first WAV while advertising its full length")
    parser.add_argument("--malformed-spectrum-once", action="store_true",
                        help="Send one malformed JSON spectrum response to test client error handling")
    parser.add_argument("--slow-http-ms-per-chunk", type=int, default=0,
                        help="Delay each 4096-byte WAV chunk for cancellation tests")
    parser.add_argument("--drop-audio-once-after-ms", type=int, default=0,
                        help="Close the first PCM connection after this many ms, for reconnect testing")
    parser.add_argument("--no-gain-control", action="store_true",
                        help="Simulate old firmware without the gain_control capability")
    parser.add_argument("--reject-gain-once", action="store_true",
                        help="Reject the first valid SET_FILTER_GAIN request")
    args = parser.parse_args()
    state = MockState(drop_audio_once_after_blocks=max(0, args.drop_audio_once_after_ms // 10),
                      truncate_download_once=args.truncate_download_once,
                      malformed_spectrum_once=args.malformed_spectrum_once,
                      http_chunk_delay_ms=max(0, args.slow_http_ms_per_chunk),
                      gain_supported=not args.no_gain_control,
                      reject_gain_once=args.reject_gain_once)
    with MockServer((args.host, args.port), state) as server, AudioServer((args.host, args.audio_port), state) as audio, WavHttpServer((args.host, args.http_port), state) as http:
        audio_thread = threading.Thread(target=audio.serve_forever, daemon=True)
        http_thread = threading.Thread(target=http.serve_forever, daemon=True)
        audio_thread.start()
        http_thread.start()
        print(f"Mock Zynq control {args.host}:{server.server_address[1]}, PCM {args.host}:{audio.server_address[1]}, WAV HTTP {args.host}:{http.server_address[1]}", flush=True)
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass
        finally:
            audio.shutdown()
            http.shutdown()
            audio_thread.join(timeout=2)
            http_thread.join(timeout=2)


if __name__ == "__main__":
    main()
