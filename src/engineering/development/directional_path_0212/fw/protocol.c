#include "protocol.h"
#include "json_min.h"
#include "app_state.h"
#include "sim_data.h"
#include "doa_capture.h"
#include "uptime.h"
#include "audio_stream.h"
#include "network.h"
#include "dhcp_server.h"
#include "mic_capture.h"
#include "spatial_runtime.h"
#include "audio_capture.h"
#include "raw8.h"
#include "adaptive.h"
#include <math.h>

#include <stdint.h>
#include <limits.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

/* Formatted writes are bounds-checked and never insert request-supplied text. */
static size_t fmt(char *out, size_t cap, const char *format, ...)
    __attribute__((format(printf, 3, 4)));
static size_t fmt(char *out, size_t cap, const char *format, ...)
{
    int n;
    va_list ap;
    if (cap == 0U) return 0U;
    va_start(ap, format);
    n = vsnprintf(out, cap, format, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= cap) {
        out[0] = '\0';
        return 0U;
    }
    return (size_t)n;
}

static size_t error_reply(char *out, size_t cap, const char *id,
                          const char *code, const char *message)
{
    return fmt(out, cap,
        "{\"v\":1,\"id\":%s,\"ok\":false,"
        "\"error\":{\"code\":\"%s\",\"message\":\"%s\"}}",
        id, code, message);
}

size_t protocol_frame_too_long(char *out, size_t cap)
{
    return error_reply(out, cap, "null", "MESSAGE_TOO_LONG",
                       "Maximum 2048 bytes per JSON line");
}

static const char *transport_name(ProtocolTransport transport)
{
    switch (transport) {
        case PROTOCOL_TRANSPORT_UART1: return "uart1";
        case PROTOCOL_TRANSPORT_TCP_CONTROL: return "ethernet_tcp";
        case PROTOCOL_TRANSPORT_BLUETOOTH_SPP: return "bluetooth_spp";
        default: return "unknown";
    }
}

static size_t info_reply(char *out, size_t cap, const char *id,
                         ProtocolTransport transport)
{
    /* Capabilities describe CURRENTLY implemented features, not future plans. */
    return fmt(out, cap,
        "{\"v\":1,\"id\":%s,\"ok\":true,\"data\":{"
          "\"device_name\":\"BX71 Eight Microphone DOA\","
          "\"firmware_version\":\"0.21.2-eight-mic-localpath\","
          "\"protocol_version\":1,\"sample_rate_hz\":%u,"
          "\"transport\":\"%s\",\"simulated\":false,"
          "\"audio_source\":\"ics43434_filtered\","
          "\"capabilities\":{"
             "\"spectrum\":true,\"sources\":true,\"multi_source\":true,\"max_sources\":4,\"angle_control\":true,"
             "\"gain_control\":true,\"beamforming\":%s,"
             "\"record_control_simulated\":false,\"real_recording\":false,"
             "\"pcm_stream\":%s,\"stream_audio\":%s,"
             "\"file_download\":false,\"storage_persistent\":false}}}",
        id, (unsigned)SIM_SAMPLE_RATE_HZ, transport_name(transport),
        mic_capture_beamforming_ready()?"true":"false",
        transport == PROTOCOL_TRANSPORT_TCP_CONTROL ? "true" : "false",
        transport == PROTOCOL_TRANSPORT_TCP_CONTROL ? "true" : "false");
}


/* One main-loop snapshot: peaks, candidates and tracks share sequence. */
static size_t debug_reply(char *out,size_t cap,const char *id)
{
    DoaDiagnostics d; size_t n,k;
    doa_capture_diagnostics(&d);
    n=fmt(out,cap,"{\"v\":1,\"id\":%s,\"ok\":true,\"data\":{\"sequence\":%lu,\"fresh\":%s,\"reason\":%lu,\"resets\":[%lu,%lu,%lu,%lu,%lu],\"expired\":%lu,\"trace_count\":%u,\"evidence\":%lu,\"source_count\":%u,\"instant\":[",
        id,(unsigned long)d.sequence,d.fresh?"true":"false",(unsigned long)d.algorithm.reason,
        (unsigned long)d.algorithm.resets[0],(unsigned long)d.algorithm.resets[1],(unsigned long)d.algorithm.resets[2],(unsigned long)d.algorithm.resets[3],(unsigned long)d.algorithm.resets[4],
        (unsigned long)d.algorithm.expired,doa_capture_trace_count(),(unsigned long)d.algorithm.evidence_mask,(unsigned)d.result.source_count);
    if(!n)return 0;
    for(unsigned group=0;group<3;++group) {
        const DoaPeakDebug *peaks=group==0?d.algorithm.instant:(group==1?d.algorithm.stable:d.algorithm.candidates);
        unsigned count=group==0?d.algorithm.instant_count:(group==1?d.algorithm.stable_count:d.algorithm.candidate_count);
        for(unsigned i=0;i<count && i<4;++i) {
            k=fmt(out+n,cap-n,"%s[%lu,%ld,%ld]",i?",":"",(unsigned long)peaks[i].angle,(long)peaks[i].level,(long)peaks[i].prominence);
            if(!k){out[0]=0;return 0;}n+=k;
        }
        k=fmt(out+n,cap-n,group==0?"],\"stable\":[":(group==1?"],\"candidates\":[":"],\"tracks\":["));
        if(!k){out[0]=0;return 0;}n+=k;
    }
    for(unsigned i=0;i<4;++i) {
        const DoaTrackDebug *t=&d.algorithm.tracks[i];
        k=fmt(out+n,cap-n,"%s[%lu,%lu,%lu,%lu,%lu]",i?",":"",(unsigned long)t->id,(unsigned long)t->angle,(unsigned long)t->recent,(unsigned long)t->misses,(unsigned long)t->flags);
        if(!k){out[0]=0;return 0;}n+=k;
    }
    k=fmt(out+n,cap-n,"],\"simulated\":false}}");
    if(!k){out[0]=0;return 0;}return n+k;
}

static size_t status_reply(char *out, size_t cap, const char *id)
{
    AppState state = app_state_get();
    return fmt(out, cap,
        "{\"v\":1,\"id\":%s,\"ok\":true,\"data\":{"
           "\"work_state\":\"%s\","
           "\"target_angle_deg\":%u,\"recording\":false,"
           "\"streaming\":%s,\"simulated\":false}}",
        id, doa_capture_state(), (unsigned)state.target_angle_deg,
        audio_stream_is_active() ? "true" : "false");
}

static size_t spectrum_reply(char *out, size_t cap, const char *id)
{
    SimSnapshot snapshot;
    size_t n;
    doa_capture_snapshot(&snapshot);
    n = fmt(out, cap,
        "{\"v\":1,\"id\":%s,\"ok\":true,\"data\":{"
        "\"step_deg\":5,\"values\":[", id);
    if (n == 0U) return 0U;

    for (unsigned i = 0U; i < SIM_SPECTRUM_POINTS; ++i) {
        size_t k = fmt(out + n, cap - n, "%s%u", i ? "," : "",
                       (unsigned)snapshot.spectrum[i]);
        if (k == 0U) { out[0] = '\0'; return 0U; }
        n += k;
    }
    {
        size_t k = fmt(out + n, cap - n, "],\"simulated\":false}}");
        if (k == 0U) { out[0] = '\0'; return 0U; }
        n += k;
    }
    return n;
}

static size_t sources_reply(char *out, size_t cap, const char *id)
{
    SimSnapshot snap;
    size_t n;
    doa_capture_snapshot(&snap);
    n = fmt(out, cap,
        "{\"v\":1,\"id\":%s,\"ok\":true,\"data\":{\"sources\":[", id);
    if (n == 0U) return 0U;
    for (unsigned i = 0U; i < snap.source_count; ++i) {
        size_t k = fmt(out + n, cap - n,
            "%s{\"source_id\":%lu,\"angle_deg\":%u,\"strength\":%u}",
            i ? "," : "", (unsigned long)snap.sources[i].source_id,
            (unsigned)snap.sources[i].angle_deg,
            (unsigned)snap.sources[i].strength);
        if (k == 0U) { out[0] = '\0'; return 0U; }
        n += k;
    }
    {
        size_t k = fmt(out + n, cap - n, "],\"simulated\":false}}");
        if (k == 0U) { out[0] = '\0'; return 0U; }
        n += k;
    }
    return n;
}

static int no_args(const JsonDoc *d, int args)
{
    return json_object_empty(d, args);
}

size_t protocol_handle_transport(const char *line, size_t length,
                                 ProtocolTransport transport,
                                 char *out, size_t cap)
{
    /* The main loop calls protocol handlers serially; keep 400 JSON tokens
     * in static RAM so a 360-value request cannot overflow the 8-KB stack. */
    static JsonDoc d;
    char idbuf[16] = "null";
    int id, version, cmd, args, r;
    uint32_t request_id, version_value;

    if (line == NULL || out == NULL || cap == 0U) return 0U;
    r = json_parse(&d, line, length);
    if (r == 2)
        return error_reply(out, cap, "null", "JSON_TOO_COMPLEX",
                           "JSON depth or token limit exceeded");
    if (r != 0)
        return error_reply(out, cap, "null", "INVALID_JSON", "Malformed JSON");
    if (d.count == 0 || d.tokens[0].type != JSON_OBJECT)
        return error_reply(out, cap, "null", "INVALID_REQUEST", "Request must be an object");

    id = json_find(&d, 0, "id");
    /* Keep the previously tested phase-2 request-ID range 0..INT32_MAX. */
    if (id == -2 || !json_get_uint32(&d, id, &request_id) || request_id > INT32_MAX)
        return error_reply(out, cap, "null", "INVALID_REQUEST",
                           "id must be a nonnegative 32-bit integer");
    (void)snprintf(idbuf, sizeof(idbuf), "%lu", (unsigned long)request_id);

    version = json_find(&d, 0, "v");
    if (version == -2 || !json_get_uint32(&d, version, &version_value))
        return error_reply(out, cap, idbuf, "INVALID_REQUEST", "v must be an integer");
    if (version_value != 1U)
        return error_reply(out, cap, idbuf, "UNSUPPORTED_VERSION",
                           "Only protocol v=1 is supported");

    cmd = json_find(&d, 0, "cmd");
    if (cmd == -2 || cmd < 0 || d.tokens[cmd].type != JSON_STRING)
        return error_reply(out, cap, idbuf, "INVALID_REQUEST", "cmd must be a string");
    args = json_find(&d, 0, "args");
    if (args == -2 || args < 0 || d.tokens[args].type != JSON_OBJECT)
        return error_reply(out, cap, idbuf, "INVALID_REQUEST", "args must be an object");

    if (json_string_equals(&d, cmd, "SET_FILTER_CUTOFF")) {
        int token = json_find(&d, args, "cutoff_hz");
        uint32_t hz;
        if (transport != PROTOCOL_TRANSPORT_UART1)
            return error_reply(out, cap, idbuf, "UNSUPPORTED_TRANSPORT",
                               "Filter cutoff is configured through UART1");
        if (token == -2 || !json_get_uint32(&d, token, &hz) ||
            hz < 100U || hz > 10000U || d.tokens[args].next != args + 3)
            return error_reply(out, cap, idbuf, "INVALID_ARGUMENT",
                               "cutoff_hz must be an integer from 100 to 10000");
        if (!mic_capture_set_cutoff(hz))
            return error_reply(out, cap, idbuf, "MIC_UNAVAILABLE",
                               "Microphone PL peripheral is unavailable");
        return fmt(out, cap,
            "{\"v\":1,\"id\":%s,\"ok\":true,\"data\":{"
            "\"cutoff_hz\":%lu,\"filter_order\":36}}",
            idbuf, (unsigned long)hz);
    }
    if (json_string_equals(&d, cmd, "SET_FILTER_ENABLE")) {
        int token = json_find(&d, args, "enabled");
        int enabled;
        if (transport != PROTOCOL_TRANSPORT_UART1)
            return error_reply(out, cap, idbuf, "UNSUPPORTED_TRANSPORT",
                               "Filter enable is configured through UART1");
        if (token == -2 || token < 0 || d.tokens[args].next != args + 3 ||
            (d.tokens[token].type != JSON_TRUE &&
             d.tokens[token].type != JSON_FALSE))
            return error_reply(out, cap, idbuf, "INVALID_ARGUMENT",
                               "enabled must be true or false");
        enabled = d.tokens[token].type == JSON_TRUE;
        if (!mic_capture_set_filter_enabled(enabled))
            return error_reply(out, cap, idbuf, "MIC_UNAVAILABLE",
                               "Microphone PL peripheral is unavailable");
        return fmt(out, cap,
            "{\"v\":1,\"id\":%s,\"ok\":true,\"data\":{"
            "\"filter_enabled\":%s,\"gain_db\":%lu}}",
            idbuf, mic_capture_filter_enabled() ? "true" : "false",
            (unsigned long)mic_capture_gain());
    }
    if (json_string_equals(&d, cmd, "SET_FILTER_GAIN")) {
        int token = json_find(&d, args, "gain_db");
        uint32_t db;
        if (token == -2 || !json_get_uint32(&d, token, &db) ||
            db > MIC_GAIN_MAX_DB || d.tokens[args].next != args + 3)
            return error_reply(out, cap, idbuf, "INVALID_ARGUMENT",
                               "gain_db must be an integer from 0 to 96");
        if (!mic_capture_set_gain(db))
            return error_reply(out, cap, idbuf, "MIC_UNAVAILABLE",
                               "Microphone PL peripheral is unavailable");
        return fmt(out, cap,
            "{\"v\":1,\"id\":%s,\"ok\":true,\"data\":{"
            "\"gain_db\":%lu}}", idbuf, (unsigned long)db);
    }
    if (json_string_equals(&d, cmd, "SET_SIM_ENERGY_360") ||
        json_string_equals(&d, cmd, "SET_SIM_SOUNDFIELD") ||
        json_string_equals(&d, cmd, "CLEAR_SIM_SOUNDFIELD"))
        return error_reply(out, cap, idbuf, "UNSUPPORTED_FEATURE",
                           "This firmware uses live eight-microphone measurements");
    if (json_string_equals(&d, cmd, "SET_ANGLE")) {
        int angle = json_find(&d, args, "angle_deg");
        uint32_t angle_value;
        /* args must have exactly one key-value pair; reject extras/duplicates. */
        if (angle == -2 || !json_get_uint32(&d, angle, &angle_value) ||
            angle_value > 359U || d.tokens[args].next != args + 3)
            return error_reply(out, cap, idbuf, "INVALID_ARGUMENT",
                               "angle_deg must be an integer from 0 to 359");
        if (!mic_capture_set_direction(angle_value))
            return error_reply(out,cap,idbuf,"BEAMFORMER_UNAVAILABLE","Matching beamforming hardware required");
        app_state_set_angle((uint16_t)angle_value);
        return fmt(out, cap,
            "{\"v\":1,\"id\":%s,\"ok\":true,\"data\":{"
            "\"angle_deg\":%u,\"simulated\":false}}",
            idbuf, (unsigned)angle_value);
    }

    if(json_string_equals(&d,cmd,"GET_CALIBRATION_BIN")){
        if(transport!=PROTOCOL_TRANSPORT_UART1)return error_reply(out,cap,idbuf,"UNSUPPORTED_TRANSPORT","Calibration diagnostic requires UART");
        uint32_t bin;AdCalBin b;
        if(!json_get_uint32(&d,json_find(&d,args,"bin"),&bin)||bin<2||bin>53||d.tokens[args].next!=args+3)return error_reply(out,cap,idbuf,"INVALID_ARGUMENT","bin2..53 required");
        if(!ad_calibration_bin(bin,&b))return error_reply(out,cap,idbuf,"CALIBRATION_NOT_READY","Complete calibration first");
        return fmt(out,cap,"{\"v\":1,\"id\":%s,\"ok\":true,\"data\":{\"bin\":%lu,\"hz\":%.2f,\"flags\":%u,\"min_coherence\":%.6f,\"min_amplitude\":%.6f,\"max_amplitude\":%.6f,\"max_phase_deg\":%.6f}}",idbuf,(unsigned long)bin,bin*93.75,b.flags,b.min_coherence,b.min_amplitude,b.max_amplitude,b.max_phase_deg);
    }
    if (json_string_equals(&d,cmd,"CALIBRATE_RAW8") || json_string_equals(&d,cmd,"CALIBRATE_PATH8") || json_string_equals(&d,cmd,"SET_ADAPTIVE")) {
        if(transport!=PROTOCOL_TRANSPORT_UART1)return error_reply(out,cap,idbuf,"UNSUPPORTED_TRANSPORT","Diagnostic control requires UART");
        int path=json_string_equals(&d,cmd,"CALIBRATE_PATH8");
        int cal=path||json_string_equals(&d,cmd,"CALIBRATE_RAW8");
        int token=json_find(&d,args,cal?"angle_deg":"enabled");uint32_t value;
        if(!json_get_uint32(&d,token,&value)||d.tokens[args].next!=args+3||value>(cal?359U:1U))return error_reply(out,cap,idbuf,"INVALID_ARGUMENT","Invalid calibration angle or adaptive enable");
        if(cal){if(!(path?raw8_calibrate_path(value):raw8_calibrate(value)))return error_reply(out,cap,idbuf,"CALIBRATION_BUSY","Raw hardware unavailable or calibration busy");}
        else ad_enable(value);
        return fmt(out,cap,"{\"v\":1,\"id\":%s,\"ok\":true,\"data\":{\"accepted\":true}}",idbuf);
    }
    if (json_string_equals(&d,cmd,"GET_RAW8_BLOCK")) {
        if(transport!=PROTOCOL_TRANSPORT_UART1)return error_reply(out,cap,idbuf,"UNSUPPORTED_TRANSPORT","Raw export requires UART");
        uint32_t offset,count,generation;int32_t words[64];char hex[513];static const char digits[]="0123456789abcdef";
        int a=json_find(&d,args,"offset"),b=json_find(&d,args,"count"),g=json_find(&d,args,"generation");
        Raw8Status s;raw8_status(&s);
        if(!json_get_uint32(&d,a,&offset)||!json_get_uint32(&d,b,&count)||!json_get_uint32(&d,g,&generation)||count<1||count>64||d.tokens[args].next!=args+7)return error_reply(out,cap,idbuf,"INVALID_ARGUMENT","offset,count1..64,generation required");
        if(!s.export_ready||generation!=s.export_generation||!raw8_read(offset,count,words))return error_reply(out,cap,idbuf,"RAW_NOT_READY","Generation or range invalid");
        for(unsigned i=0;i<count;i++)for(unsigned j=0;j<4;j++){unsigned byte=((uint32_t)words[i]>>(8*j))&255;hex[8*i+2*j]=digits[byte>>4];hex[8*i+2*j+1]=digits[byte&15];}
        hex[8*count]=0;
        return fmt(out,cap,"{\"v\":1,\"id\":%s,\"ok\":true,\"data\":{\"generation\":%lu,\"offset\":%lu,\"count\":%lu,\"s32le_hex\":\"%s\"}}",idbuf,(unsigned long)generation,(unsigned long)offset,(unsigned long)count,hex);
    }
    if (json_string_equals(&d, cmd, "GET_AUDIO_BLOCK")) {
        uint32_t offset,count;int16_t samples[128];char hex[513];
        static const char digits[]="0123456789abcdef";
        if(transport!=PROTOCOL_TRANSPORT_UART1)
            return error_reply(out,cap,idbuf,"UNSUPPORTED_TRANSPORT","Diagnostic capture requires UART");
        int a=json_find(&d,args,"offset"),b=json_find(&d,args,"count");
        if(!json_get_uint32(&d,a,&offset)||!json_get_uint32(&d,b,&count)||
           count==0||count>128||d.tokens[args].next!=args+5)
            return error_reply(out,cap,idbuf,"INVALID_ARGUMENT","offset and count 1..128 required");
        if(!ac_read(offset,count,samples))
            return error_reply(out,cap,idbuf,"CAPTURE_NOT_READY","Capture not frozen or range invalid");
        for(unsigned i=0;i<count;i++){
            uint16_t x=(uint16_t)samples[i];
            hex[4*i]=digits[(x>>4)&15];hex[4*i+1]=digits[x&15];
            hex[4*i+2]=digits[(x>>12)&15];hex[4*i+3]=digits[(x>>8)&15];
        }
        hex[4*count]=0;ACStatus capture;ac_status(&capture);
        return fmt(out,cap,"{\"v\":1,\"id\":%s,\"ok\":true,\"data\":{\"generation\":%lu,\"offset\":%lu,\"count\":%lu,\"s16le_hex\":\"%s\"}}",idbuf,(unsigned long)capture.generation,(unsigned long)offset,(unsigned long)count,hex);
    }
    if (json_string_equals(&d, cmd, "PING") ||
        json_string_equals(&d, cmd, "CAPTURE_RAW8") ||
        json_string_equals(&d, cmd, "GET_ADAPTIVE_STATUS") ||
        json_string_equals(&d, cmd, "ARM_AUDIO_CAPTURE") ||
        json_string_equals(&d, cmd, "GET_AUDIO_CAPTURE") ||
        json_string_equals(&d, cmd, "GET_INFO") ||
        json_string_equals(&d, cmd, "GET_FILTER_STATUS") ||
        json_string_equals(&d, cmd, "GET_DOA_STATUS") ||
        json_string_equals(&d, cmd, "GET_DOA_DEBUG") ||
        json_string_equals(&d, cmd, "ARM_DOA_TRACE") ||
        json_string_equals(&d, cmd, "GET_NET_DIAG") ||
        json_string_equals(&d, cmd, "GET_STATUS") ||
        json_string_equals(&d, cmd, "GET_SPECTRUM") ||
        json_string_equals(&d, cmd, "GET_SOURCES") ||
        json_string_equals(&d, cmd, "START_RECORD") ||
        json_string_equals(&d, cmd, "STOP_RECORD") ||
        json_string_equals(&d, cmd, "LIST_RECORDINGS") ||
        json_string_equals(&d, cmd, "START_STREAM") ||
        json_string_equals(&d, cmd, "STOP_STREAM")) {
        if (!no_args(&d, args))
            return error_reply(out, cap, idbuf, "INVALID_ARGUMENT",
                               "Command requires empty args");
    } else {
        return error_reply(out, cap, idbuf, "UNKNOWN_COMMAND", "Unknown command");
    }

    if (json_string_equals(&d, cmd, "PING"))
        return fmt(out, cap,
            "{\"v\":1,\"id\":%s,\"ok\":true,\"data\":{\"uptime_ms\":%lu}}",
            idbuf, (unsigned long)uptime_millis());
    if (json_string_equals(&d, cmd, "GET_INFO")) return info_reply(out, cap, idbuf, transport);
    if(json_string_equals(&d,cmd,"CAPTURE_RAW8")){
        if(transport!=PROTOCOL_TRANSPORT_UART1)return error_reply(out,cap,idbuf,"UNSUPPORTED_TRANSPORT","Raw capture requires UART");
        if(!raw8_export_arm())return error_reply(out,cap,idbuf,"RAW_BUSY","Raw capture unavailable or busy");
        return fmt(out,cap,"{\"v\":1,\"id\":%s,\"ok\":true,\"data\":{\"armed\":true}}",idbuf);
    }
    if(json_string_equals(&d,cmd,"GET_ADAPTIVE_STATUS")){
        AdStatus a;Raw8Status r;ad_status(&a);raw8_status(&r);
        return fmt(out,cap,"{\"v\":1,\"id\":%s,\"ok\":true,\"data\":{\"raw_supported\":%lu,\"raw_state\":%lu,\"raw_records\":%lu,\"raw_errors\":%lu,\"raw_clips\":%lu,\"raw_max_poll_ms\":%lu,\"export_ready\":%lu,\"export_generation\":%lu,\"export_first\":%lu,\"export_last\":%lu,\"export_clips\":%lu,\"channels\":8,\"frames_per_capture\":1024,\"bits\":24,\"sample_rate_hz\":48000,\"covariance_frames\":%lu,\"adaptive_epoch\":%lu,\"adaptive_enabled\":%lu,\"accepted_bins\":%lu,\"rejected_bins\":%lu,\"cal_state\":%lu,\"cal_angle\":%lu,\"cal_bins\":%lu,\"cal_power_bins\":%lu,\"cal_coherent_bins\":%lu,\"cal_amplitude_bins\":%lu,\"cal_phase_bins\":%lu,\"cal_mode\":%lu,\"cal_stage\":%lu,\"path_bins\":%lu,\"path_angle\":%lu}}",idbuf,
        (unsigned long)r.supported,(unsigned long)r.state,(unsigned long)r.records,(unsigned long)r.errors,(unsigned long)r.clips,(unsigned long)r.max_poll_ms,(unsigned long)r.export_ready,(unsigned long)r.export_generation,(unsigned long)r.export_first,(unsigned long)r.export_last,(unsigned long)r.export_clips,
        (unsigned long)a.frames,(unsigned long)a.epoch,(unsigned long)a.enabled,(unsigned long)a.accepted,(unsigned long)a.rejected,(unsigned long)a.cal_state,(unsigned long)a.cal_angle,(unsigned long)a.cal_bins,(unsigned long)a.cal_power_bins,(unsigned long)a.cal_coherent_bins,(unsigned long)a.cal_amplitude_bins,(unsigned long)a.cal_phase_bins,(unsigned long)a.cal_mode,(unsigned long)a.cal_stage,(unsigned long)a.path_bins,(unsigned long)a.path_angle);
    }
    if (json_string_equals(&d, cmd, "ARM_AUDIO_CAPTURE") || json_string_equals(&d,cmd,"GET_AUDIO_CAPTURE")) {
        if(transport!=PROTOCOL_TRANSPORT_UART1)
            return error_reply(out,cap,idbuf,"UNSUPPORTED_TRANSPORT","Diagnostic capture requires UART");
        if(json_string_equals(&d,cmd,"ARM_AUDIO_CAPTURE"))mic_capture_audio_context();
        if(json_string_equals(&d,cmd,"ARM_AUDIO_CAPTURE")&&!ac_arm())
            return error_reply(out,cap,idbuf,"CAPTURE_BUSY","Wait for idle active FIR and previous capture completion");
        ACStatus capture;ac_status(&capture);
        double mean=capture.count?(double)capture.sum/capture.count:0;
        double power=capture.count?(double)capture.squares/capture.count:0;
        return fmt(out,cap,"{\"v\":1,\"id\":%s,\"ok\":true,\"data\":{\"generation\":%lu,\"state\":%lu,\"count\":%lu,\"valid\":%lu,\"sample_rate_hz\":48000,\"discard_samples\":1024,\"mean\":%.6f,\"ac_rms\":%.6f,\"peak\":%lu,\"rails\":%lu,\"target\":%lu,\"interference\":%lu,\"updates\":%lu,\"gain_db\":%lu,\"fifo\":%lu}}",idbuf,
            (unsigned long)capture.generation,(unsigned long)capture.state,
            (unsigned long)capture.count,(unsigned long)capture.valid,mean,
            sqrt(fmax(0,power-mean*mean)),(unsigned long)capture.peak,(unsigned long)capture.rails,
            (unsigned long)capture.context.target,(unsigned long)capture.context.interference,
            (unsigned long)capture.context.updates,(unsigned long)capture.context.gain,
            (unsigned long)capture.context.fifo);
    }
    if (json_string_equals(&d, cmd, "GET_DOA_DEBUG")) return debug_reply(out,cap,idbuf);
    if (json_string_equals(&d, cmd, "ARM_DOA_TRACE")) {
        if (transport != PROTOCOL_TRANSPORT_UART1)
            return error_reply(out,cap,idbuf,"UNSUPPORTED_TRANSPORT","Diagnostic capture requires UART");
        if (!doa_capture_trace_arm())
            return error_reply(out,cap,idbuf,"TRACE_BUSY","Capture busy or array unavailable");
        return fmt(out,cap,"{\"v\":1,\"id\":%s,\"ok\":true,\"data\":{\"armed\":true,\"frames\":512}}",idbuf);
    }
    if (json_string_equals(&d, cmd, "GET_DOA_STATUS")) {
        DoaDiagnostics diag;
        doa_capture_diagnostics(&diag);
        return fmt(out, cap,
            "{\"v\":1,\"id\":%s,\"ok\":true,\"data\":{"
            "\"array_ready\":%s,\"fresh\":%s,\"valid\":%s,"
            "\"sequence\":%lu,\"frames_processed\":%lu,\"age_ms\":%lu,"
            "\"sample_count\":%lu,\"overruns\":%lu,\"active_mask\":%u,"
            "\"rms\":[%u,%u,%u,%u,%u,%u,%u,%u],\"coherence\":%u,\"prominence\":%u,"
            "\"angle_deg\":%u,\"source_count\":%u,\"max_sources\":4,\"sector_start_deg\":0,\"sector_end_deg\":359,"
            "\"radius_mm\":60,\"microphones\":[1,2,3,4,5,6,7,8],"
            "\"sample_rate_hz\":48000,\"simulated\":false}}",
            idbuf, diag.ready ? "true" : "false", diag.fresh ? "true" : "false",
            diag.result.valid ? "true" : "false",
            (unsigned long)diag.sequence, (unsigned long)diag.frames_processed,
            (unsigned long)diag.age_ms, (unsigned long)diag.sample_count,
            (unsigned long)diag.overruns, (unsigned)diag.result.active_mask,
            (unsigned)diag.result.rms[0], (unsigned)diag.result.rms[1],
            (unsigned)diag.result.rms[2], (unsigned)diag.result.rms[3],
            (unsigned)diag.result.rms[4], (unsigned)diag.result.rms[5],
            (unsigned)diag.result.rms[6], (unsigned)diag.result.rms[7],
            (unsigned)diag.result.coherence_milli,
            (unsigned)diag.result.prominence_milli, (unsigned)diag.result.angle_deg,
            (unsigned)diag.result.source_count);
    }
    if (json_string_equals(&d, cmd, "GET_FILTER_STATUS")) {
        SFControlStatus spatial;spatial_runtime_status(&spatial);
        return fmt(out, cap,
            "{\"v\":1,\"id\":%s,\"ok\":true,\"data\":{"
            "\"mic_ready\":%s,\"cutoff_hz\":%lu,\"gain_db\":%lu,"
            "\"filter_enabled\":%s,"
            "\"filter_order\":36,\"sample_count\":%lu,"
            "\"fifo_overflows\":%lu,\"filter_overruns\":%lu,"
            "\"stream_buffer_overflows\":%lu,\"beamforming_ready\":%s,\"beam_overruns\":%lu,\"effective_gain_q16\":%lu,\"limited_samples\":%lu,"
            "\"spatial_supported\":%lu,\"spatial_active\":%u,\"spatial_phase\":%lu,\"spatial_target_deg\":%lu,\"spatial_interference_deg\":%lu,\"spatial_updates\":%lu,\"spatial_failures\":%lu,\"spatial_max_poll_ms\":%lu,\"spatial_rejected_designs\":%lu,\"spatial_design_error\":%lu}}",
            idbuf, mic_capture_ready() ? "true" : "false",
            (unsigned long)mic_capture_cutoff(),
            (unsigned long)mic_capture_gain(),
            mic_capture_filter_enabled() ? "true" : "false",
            (unsigned long)mic_capture_samples(),
            (unsigned long)mic_capture_fifo_overflows(),
            (unsigned long)mic_capture_filter_overruns(),
            (unsigned long)audio_stream_overflows(),
            mic_capture_beamforming_ready()?"true":"false",
            (unsigned long)mic_capture_beam_overruns(),
            (unsigned long)mic_capture_effective_gain(),
            (unsigned long)mic_capture_limited_samples(),
            (unsigned long)spatial.supported,spatial_runtime_active(),
            (unsigned long)spatial.phase,(unsigned long)spatial.applied_target,
            (unsigned long)spatial.applied_interference,(unsigned long)spatial.updates,
            (unsigned long)spatial.failures,(unsigned long)spatial.max_poll_ms,(unsigned long)spatial.rejected_designs,(unsigned long)spatial.last_design_error);
    }
    if (json_string_equals(&d, cmd, "GET_NET_DIAG")) {
        network_diag_refresh_hw();
        return fmt(out, cap,
            "{\"v\":1,\"id\":%s,\"ok\":true,\"data\":{"
            "\"init_stage\":%lu,\"polls\":%lu,\"link_up\":%lu,"
            "\"rx_frames\":%lu,\"rx_ipv4\":%lu,\"rx_arp\":%lu,"
            "\"rx_ipv6\":%lu,\"last_ethertype\":%lu,\"last_src_oui\":%lu,"
            "\"gem_rx_frames\":%lu,\"gem_rx_broadcast\":%lu,"
            "\"gem_rx_fcs_errors\":%lu,"
            "\"dhcp_rx\":%lu,\"dhcp_valid\":%lu,"
            "\"discovers\":%lu,\"requests\":%lu,"
            "\"replies_sent\":%lu,\"send_errors\":%lu,"
            "\"last_rx_len\":%lu,\"last_type\":%lu,"
            "\"udp67_frames\":%lu,\"last_src_ip\":%lu,"
            "\"last_dst_ip\":%lu,\"last_udp_src\":%lu,"
            "\"last_udp_dst\":%lu,\"last_ip_proto\":%lu,"
            "\"last_frame_len\":%lu}}",
            idbuf, (unsigned long)network_diag[0],
            (unsigned long)network_diag[1], (unsigned long)network_diag[2],
            (unsigned long)network_diag[3], (unsigned long)network_diag[4],
            (unsigned long)network_diag[5], (unsigned long)network_diag[6],
            (unsigned long)network_diag[7], (unsigned long)network_diag[8],
            (unsigned long)network_diag[9], (unsigned long)network_diag[10],
            (unsigned long)network_diag[11],
            (unsigned long)dhcp_diag[0], (unsigned long)dhcp_diag[1],
            (unsigned long)dhcp_diag[2], (unsigned long)dhcp_diag[3],
            (unsigned long)dhcp_diag[4], (unsigned long)dhcp_diag[5],
            (unsigned long)dhcp_diag[6], (unsigned long)dhcp_diag[7],
            (unsigned long)network_diag[12], (unsigned long)network_diag[13],
            (unsigned long)network_diag[14], (unsigned long)network_diag[15],
            (unsigned long)network_diag[16], (unsigned long)network_diag[17],
            (unsigned long)network_diag[18]);
    }
    if (json_string_equals(&d, cmd, "GET_STATUS")) return status_reply(out, cap, idbuf);
    if (json_string_equals(&d, cmd, "GET_SPECTRUM")) return spectrum_reply(out, cap, idbuf);
    if (json_string_equals(&d, cmd, "GET_SOURCES")) return sources_reply(out, cap, idbuf);
    if (json_string_equals(&d, cmd, "START_RECORD") ||
        json_string_equals(&d, cmd, "STOP_RECORD"))
        return error_reply(out, cap, idbuf, "UNSUPPORTED_FEATURE",
                           "Record on the phone using START_STREAM PCM");
    if (json_string_equals(&d, cmd, "LIST_RECORDINGS"))
        return fmt(out, cap,
            "{\"v\":1,\"id\":%s,\"ok\":true,\"data\":{"
            "\"recordings\":[],\"simulated\":false,"
            "\"storage_persistent\":false}}",
            idbuf);

    if (transport != PROTOCOL_TRANSPORT_TCP_CONTROL)
        return error_reply(out, cap, idbuf, "UNSUPPORTED_TRANSPORT",
                           "Audio streaming requires Ethernet TCP");
    if (json_string_equals(&d, cmd, "START_STREAM")) {
        if (!spatial_runtime_stream_ready())
            return error_reply(out,cap,idbuf,"SPATIAL_NOT_READY",
                               "Spatial filter initializing or faulted; query GET_FILTER_STATUS");
        if (!audio_stream_start())
            return error_reply(out, cap, idbuf, "STREAM_UNAVAILABLE",
                               "Audio server is not initialized");
        return fmt(out, cap,
            "{\"v\":1,\"id\":%s,\"ok\":true,\"data\":{"
            "\"sample_rate_hz\":48000,\"channels\":1,"
            "\"bits_per_sample\":16,\"encoding\":\"S16LE\","
            "\"port\":9000,\"simulated\":false}}", idbuf);
    }
    audio_stream_stop();
    return fmt(out, cap,
        "{\"v\":1,\"id\":%s,\"ok\":true,\"data\":{"
        "\"streaming\":false,\"simulated\":false}}", idbuf);
}

size_t protocol_handle(const char *line, size_t length,
                       char *response, size_t response_capacity)
{
    return protocol_handle_transport(line, length, PROTOCOL_TRANSPORT_UART1,
                                     response, response_capacity);
}
