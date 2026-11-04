import json
import socket
import struct
import threading
import unittest
import urllib.error
import urllib.request
import io
import http.client
import wave

from mock_zynq import AudioServer, MockServer, MockState, WavHttpServer, gained_sample


class MockServerTest(unittest.TestCase):
    def setUp(self):
        state = MockState()
        self.server = MockServer(("127.0.0.1", 0), state)
        self.audio_server = AudioServer(("127.0.0.1", 0), state)
        self.http_server = WavHttpServer(("127.0.0.1", 0), state)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.audio_thread = threading.Thread(target=self.audio_server.serve_forever, daemon=True)
        self.http_thread = threading.Thread(target=self.http_server.serve_forever, daemon=True)
        self.thread.start()
        self.audio_thread.start()
        self.http_thread.start()
        self.socket = socket.create_connection(self.server.server_address, timeout=2)
        self.socket.settimeout(2)
        self.reader = self.socket.makefile("rb")

    def tearDown(self):
        self.reader.close()
        self.socket.close()
        self.server.shutdown()
        self.audio_server.shutdown()
        self.http_server.shutdown()
        self.server.server_close()
        self.audio_server.server_close()
        self.http_server.server_close()
        self.thread.join(timeout=2)
        self.audio_thread.join(timeout=2)
        self.http_thread.join(timeout=2)

    def response(self):
        return json.loads(self.reader.readline())

    def request(self, request_id, command, args=None):
        payload = {"v": 1, "id": request_id, "cmd": command, "args": args or {}}
        self.socket.sendall((json.dumps(payload) + "\n").encode())
        response = self.response()
        self.assertEqual(request_id, response["id"])
        return response

    def test_gain_capability_read_set_boundaries_and_rejection(self):
        self.assertTrue(self.request(70, "GET_INFO")["data"]["capabilities"]["gain_control"])
        self.assertEqual(0, self.request(71, "GET_FILTER_STATUS")["data"]["gain_db"])
        for request_id, gain in ((72, 30), (73, 96), (74, 0)):
            self.assertEqual(gain, self.request(request_id, "SET_FILTER_GAIN", {"gain_db": gain})["data"]["gain_db"])
            self.assertEqual(gain, self.request(request_id + 10, "GET_FILTER_STATUS")["data"]["gain_db"])
        for request_id, bad in ((90, -1), (91, 97), (92, 30.5), (93, True)):
            self.assertEqual("INVALID_ARGUMENT", self.request(request_id, "SET_FILTER_GAIN", {"gain_db": bad})["error"]["code"])
        self.assertEqual(0, self.request(94, "GET_FILTER_STATUS")["data"]["gain_db"])
        self.server.state.reject_gain_once = True
        self.assertEqual("GAIN_REJECTED", self.request(95, "SET_FILTER_GAIN", {"gain_db": 20})["error"]["code"])
        self.assertEqual(0, self.request(96, "GET_FILTER_STATUS")["data"]["gain_db"])

    def test_legacy_gain_capability_and_pcm_clipping(self):
        self.server.state.gain_supported = False
        self.assertFalse(self.request(97, "GET_INFO")["data"]["capabilities"]["gain_control"])
        self.assertEqual("UNKNOWN_COMMAND", self.request(98, "GET_FILTER_STATUS")["error"]["code"])
        self.assertEqual("UNKNOWN_COMMAND", self.request(99, "SET_FILTER_GAIN", {"gain_db": 30})["error"]["code"])
        self.assertEqual(32767, gained_sample(9000, 96))
        self.assertEqual(-32768, gained_sample(-9000, 96))

    def test_confirmed_gain_changes_stream_and_new_recording_samples(self):
        self.assertEqual(30, self.request(100, "SET_FILTER_GAIN", {"gain_db": 30})["data"]["gain_db"])
        port = self.request(101, "START_STREAM")["data"]["port"]
        with socket.create_connection(("127.0.0.1", port), timeout=2) as audio:
            audio.settimeout(2)
            block = b""
            while len(block) < 960:
                block += audio.recv(960 - len(block))
            samples = struct.unpack("<480h", block)
            self.assertIn(32767, samples)
            self.assertIn(-32768, samples)
        self.request(102, "STOP_STREAM")
        self.request(103, "START_RECORD")
        self.request(104, "STOP_RECORD")
        entries = self.request(105, "LIST_RECORDINGS")["data"]["recordings"]
        new_id = entries[-1]["id"]
        url = f"http://127.0.0.1:{self.http_server.server_address[1]}/api/recordings/{new_id}"
        with urllib.request.urlopen(url, timeout=2) as response:
            with wave.open(io.BytesIO(response.read()), "rb") as reader:
                frames = reader.readframes(480)
        recorded = struct.unpack("<480h", frames)
        self.assertIn(32767, recorded)
        self.assertIn(-32768, recorded)

    def test_split_and_combined_frames_preserve_ids_and_state(self):
        first = b'{"v":1,"id":3,"cmd":"SET_ANGLE","args":{"angle_deg":123}}\n'
        second = b'{"v":1,"id":4,"cmd":"GET_STATUS","args":{}}\n'
        self.socket.sendall(first[:19])
        self.socket.sendall(first[19:] + second)
        self.assertEqual(3, self.response()["id"])
        status = self.response()
        self.assertEqual(4, status["id"])
        self.assertEqual(123, status["data"]["target_angle_deg"])

    def test_spectrum_and_invalid_angle(self):
        self.socket.sendall(b'{"v":1,"id":5,"cmd":"GET_SPECTRUM","args":{}}\n')
        result = self.response()
        self.assertEqual(72, len(result["data"]["values"]))
        self.assertTrue(all(0 <= value <= 1000 for value in result["data"]["values"]))
        self.socket.sendall(b'{"v":1,"id":6,"cmd":"SET_ANGLE","args":{"angle_deg":360}}\n')
        self.assertEqual("INVALID_ARGUMENT", self.response()["error"]["code"])

    def test_all_control_commands_have_protocol_responses(self):
        commands = ["PING", "GET_INFO", "GET_STATUS", "GET_SPECTRUM", "GET_SOURCES",
                    "START_RECORD", "STOP_RECORD", "LIST_RECORDINGS", "START_STREAM", "STOP_STREAM"]
        for index, command in enumerate(commands, start=10):
            request = {"v": 1, "id": index, "cmd": command, "args": {}}
            self.socket.sendall((json.dumps(request) + "\n").encode())
            response = self.response()
            self.assertEqual(index, response["id"])
            self.assertEqual(1, response["v"])
            self.assertTrue(response["ok"])

    def test_pcm_is_48khz_mono_s16le_and_stop_closes_stream(self):
        self.socket.sendall(b'{"v":1,"id":30,"cmd":"START_STREAM","args":{}}\n')
        response = self.response()
        self.assertEqual(48000, response["data"]["sample_rate_hz"])
        self.assertEqual(1, response["data"]["channels"])
        self.assertEqual(16, response["data"]["bits_per_sample"])
        self.assertEqual("S16LE", response["data"]["encoding"])
        with socket.create_connection(("127.0.0.1", response["data"]["port"]), timeout=2) as audio:
            audio.settimeout(2)
            block = b""
            while len(block) < 960:
                block += audio.recv(960 - len(block))
            samples = struct.unpack("<480h", block)
            self.assertEqual(0, samples[0])
            self.assertTrue(any(value > 0 for value in samples[1:]))
            self.assertTrue(any(value < 0 for value in samples[1:]))
            self.socket.sendall(b'{"v":1,"id":31,"cmd":"STOP_STREAM","args":{}}\n')
            self.assertTrue(self.response()["ok"])
            while audio.recv(4096):
                pass

    def test_one_time_pcm_drop_allows_new_stream(self):
        self.server.state.drop_audio_once_after_blocks = 5
        self.socket.sendall(b'{"v":1,"id":40,"cmd":"START_STREAM","args":{}}\n')
        port = self.response()["data"]["port"]
        with socket.create_connection(("127.0.0.1", port), timeout=2) as audio:
            audio.settimeout(2)
            received = 0
            while True:
                chunk = audio.recv(4096)
                if not chunk:
                    break
                received += len(chunk)
            self.assertGreaterEqual(received, 960)
        self.socket.sendall(b'{"v":1,"id":41,"cmd":"STOP_STREAM","args":{}}\n')
        self.response()
        self.socket.sendall(b'{"v":1,"id":42,"cmd":"START_STREAM","args":{}}\n')
        self.assertTrue(self.response()["ok"])
        with socket.create_connection(("127.0.0.1", port), timeout=2) as audio:
            audio.settimeout(2)
            self.assertTrue(audio.recv(960))

    def test_wav_list_download_and_recording(self):
        self.socket.sendall(b'{"v":1,"id":50,"cmd":"LIST_RECORDINGS","args":{}}\n')
        items = self.response()["data"]["recordings"]
        self.assertEqual("demo", items[0]["id"])
        url = f"http://127.0.0.1:{self.http_server.server_address[1]}/api/recordings/demo"
        with urllib.request.urlopen(url, timeout=2) as response:
            payload = response.read()
            self.assertEqual("audio/wav", response.headers["Content-Type"])
        self.assertEqual(items[0]["size_bytes"], len(payload))
        with wave.open(io.BytesIO(payload), "rb") as reader:
            self.assertEqual((1, 2, 48000),
                             (reader.getnchannels(), reader.getsampwidth(), reader.getframerate()))
            self.assertEqual(96000, reader.getnframes())
        self.socket.sendall(b'{"v":1,"id":51,"cmd":"START_RECORD","args":{}}\n')
        self.response()
        self.socket.sendall(b'{"v":1,"id":52,"cmd":"STOP_RECORD","args":{}}\n')
        self.response()
        self.socket.sendall(b'{"v":1,"id":53,"cmd":"LIST_RECORDINGS","args":{}}\n')
        self.assertEqual(2, len(self.response()["data"]["recordings"]))
        with self.assertRaises(urllib.error.HTTPError) as failure:
            urllib.request.urlopen(url + "-missing", timeout=2)
        self.assertEqual(404, failure.exception.code)
        failure.exception.close()

    def test_truncated_wav_response(self):
        self.server.state.truncate_download_once = True
        url = f"http://127.0.0.1:{self.http_server.server_address[1]}/api/recordings/demo"
        with urllib.request.urlopen(url, timeout=2) as response:
            with self.assertRaises(http.client.IncompleteRead) as failure:
                response.read()
            self.assertLess(len(failure.exception.partial), int(response.headers["Content-Length"]))
        with urllib.request.urlopen(url, timeout=2) as response:
            self.assertEqual(len(response.read()), int(response.headers["Content-Length"]))

    def test_malformed_spectrum_response_is_one_time(self):
        self.server.state.malformed_spectrum_once = True
        self.socket.sendall(b'{"v":1,"id":60,"cmd":"GET_SPECTRUM","args":{}}\n')
        with self.assertRaises(json.JSONDecodeError):
            self.response()
        self.socket.sendall(b'{"v":1,"id":61,"cmd":"GET_SPECTRUM","args":{}}\n')
        self.assertEqual(72, len(self.response()["data"]["values"]))


if __name__ == "__main__":
    unittest.main()
