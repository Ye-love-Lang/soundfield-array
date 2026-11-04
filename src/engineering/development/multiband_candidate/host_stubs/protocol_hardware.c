/* Host-only dependencies for actual protocol.c. No TCP, DMA or FPGA model.
 * The DOA result comes from the actual doa_math.c fed independent waveforms.
 * Capture readiness/freshness are injected at the protocol boundary. */
#include "doa_capture.h"
#include "app_state.h"
#include "uptime.h"
#include "audio_stream.h"
#include "mic_capture.h"
#include "network.h"
#include "dhcp_server.h"
#include <string.h>

static DoaDiagnostics diagnostic;
static int streaming, filter_enabled;
static uint32_t cutoff_hz, gain_db;
volatile uint32_t network_diag[19];
volatile uint32_t dhcp_diag[8];

void test_reset(void)
{
    memset(&diagnostic, 0, sizeof(diagnostic));
    diagnostic.ready = 1;
    streaming = 0; filter_enabled = 1; cutoff_hz = 900; gain_db = 0;
    memset((void *)network_diag, 0, sizeof(network_diag));
    memset((void *)dhcp_diag, 0, sizeof(dhcp_diag));
    network_diag[0] = 5; network_diag[2] = 1;
    app_state_init(); uptime_init(); doa_math_init();
}

void test_feed_frame(const DoaFrame *frame)
{
    doa_math_process(frame, &diagnostic.result);
    doa_math_debug(&diagnostic.algorithm);
    diagnostic.ready = diagnostic.fresh = 1;
    diagnostic.age_ms = 0;
    diagnostic.sequence++;
    diagnostic.frames_processed++;
    diagnostic.sample_count += DOA_FRAME_SAMPLES;
}

void test_set_presence(int ready, int fresh)
{
    diagnostic.ready = ready;
    diagnostic.fresh = fresh;
    diagnostic.age_ms = fresh ? 0 : 300;
}

void doa_capture_diagnostics(DoaDiagnostics *out)
{
    *out = diagnostic;
    if (!out->ready || !out->fresh)
        memset(&out->result, 0, sizeof(out->result));
}

void doa_capture_snapshot(SimSnapshot *out)
{
    DoaDiagnostics d;
    memset(out, 0, sizeof(*out));
    doa_capture_diagnostics(&d);
    if (!d.ready || !d.fresh) return;
    memcpy(out->spectrum, d.result.spectrum, sizeof(out->spectrum));
    if (d.result.valid) {
        out->source_count = d.result.source_count;
        for(unsigned i=0; i<out->source_count && i<SIM_SOURCE_COUNT; ++i) {
            out->sources[i].source_id = d.result.sources[i].source_id;
            out->sources[i].angle_deg = d.result.sources[i].angle_deg;
            out->sources[i].strength = d.result.sources[i].strength;
        }
    }
}

const char *doa_capture_state(void)
{
    if (!diagnostic.ready) return "array_unavailable";
    if (!diagnostic.fresh) return "waiting_for_samples";
    return diagnostic.result.valid ? "tracking" : "no_reliable_source";
}

int audio_stream_start(void) { streaming = 1; return 1; }
void audio_stream_stop(void) { streaming = 0; }
int audio_stream_is_active(void) { return streaming; }
uint32_t audio_stream_overflows(void) { return 0; }
void network_diag_refresh_hw(void) { }
int mic_capture_ready(void) { return diagnostic.ready; }
int mic_capture_set_cutoff(uint32_t hz) { cutoff_hz = hz; return 1; }
uint32_t mic_capture_cutoff(void) { return cutoff_hz; }
int mic_capture_set_filter_enabled(int enabled) { filter_enabled = enabled; return 1; }
int mic_capture_filter_enabled(void) { return filter_enabled; }
int mic_capture_set_gain(uint32_t db) { gain_db = db; return 1; }
uint32_t mic_capture_gain(void) { return gain_db; }
uint32_t mic_capture_samples(void) { return diagnostic.sample_count; }
uint32_t mic_capture_fifo_overflows(void) { return 0; }
uint32_t mic_capture_filter_overruns(void) { return 0; }

int doa_capture_trace_arm(void) { return diagnostic.ready; }
unsigned doa_capture_trace_count(void) { return 512U; }

int mic_capture_beamforming_ready(void){return diagnostic.ready;}
int mic_capture_set_direction(uint32_t a){return diagnostic.ready && a<360;}
uint32_t mic_capture_effective_gain(void){return 65536U;}
uint32_t mic_capture_limited_samples(void){return 0;}
uint32_t mic_capture_beam_overruns(void){return 0;}
