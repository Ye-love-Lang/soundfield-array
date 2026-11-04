#include "doa_capture.h"
#include "uptime.h"
#include "xil_io.h"
#include "xil_cache.h"
#include <string.h>

#define BASE ((UINTPTR)0x43C00000U)
#define MAGIC 0x41525238U
#define STALE_MS 250U
/* Bounded diagnostic capture. A boot captures 512 consecutive processed
 * frames, then freezes. It neither stops PL nor streams data over UART.
 * Replay uses measured correlations, not reconstructed/artificial sources.
 * Debugger may set sf8_trace_count=0 and sf8_trace_arm=1 to capture again.
 * Completion flush makes the frozen buffer visible to JTAG. */
#define SF8_TRACE_FRAMES 512U
typedef struct { uint32_t sequence, milliseconds; DoaFrame frame; } Sf8TraceFrame;
Sf8TraceFrame sf8_trace[SF8_TRACE_FRAMES];
typedef struct { DoaDebug algorithm; DoaResult result; } Sf8DebugFrame;
Sf8DebugFrame sf8_debug_trace[SF8_TRACE_FRAMES];
volatile uint32_t sf8_trace_count;
volatile uint32_t sf8_trace_arm=1U;
static DoaFrame frame;
static DoaDiagnostics diagnostic;
static uint32_t frame_at, request_at, poll_at;
static int requested;
static uint32_t rd(unsigned offset) { return Xil_In32(BASE+offset); }
static void wr(unsigned offset, uint32_t value) { Xil_Out32(BASE+offset,value); }

int doa_capture_init(void)
{
    memset(&diagnostic,0,sizeof(diagnostic));
    diagnostic.ready = rd(0x40U) == MAGIC;
    requested = 0;
    frame_at = request_at = poll_at = uptime_millis();
    doa_math_init();
    if (diagnostic.ready) wr(0x60U,0U);
    return diagnostic.ready;
}

void doa_capture_poll(void)
{
    uint32_t now = uptime_millis(), flags, seq, lo, hi;
    unsigned p, l, c;
    if (!diagnostic.ready) return;
    /* Asynchronous snapshot handshake: never busy-wait and starve lwIP. */
    if (!requested) {
        if ((uint32_t)(now-poll_at) < 20U) return;
        poll_at = now;
        if ((rd(0x60U)&4U)==0U) return;
        wr(0x60U,1U);
        request_at=now;
        requested=1;
        return;
    }
    flags=rd(0x60U);
    if ((flags&3U)!=1U) {
        if ((uint32_t)(now-request_at)>100U) { wr(0x60U,0U); requested=0; }
        return;
    }
    seq=rd(0x44U);
    if (diagnostic.frames_processed == 0U || seq!=diagnostic.sequence) {
        for(p=0;p<DOA_PAIRS;++p) for(l=0;l<DOA_LAGS;++l) {
            wr(0x48U,p*DOA_LAGS+l);
            lo=rd(0x4CU); hi=rd(0x50U);
            frame.correlation[p][l]=(int64_t)(((uint64_t)hi<<32)|lo);
        }
        for(c=0;c<DOA_CHANNELS;++c) {
            wr(0x54U,c); lo=rd(0x58U); hi=rd(0x5CU);
            frame.energy[c]=((uint64_t)hi<<32)|lo;
        }
        diagnostic.sequence=seq;
        diagnostic.overruns=rd(0x64U);
        diagnostic.sample_count=rd(0x68U);
        /* Release the PL result before trigonometry/normalization work. */
        wr(0x60U,0U); requested=0;
        doa_math_process(&frame,&diagnostic.result);
        doa_math_debug(&diagnostic.algorithm);
        if (sf8_trace_arm && sf8_trace_count < SF8_TRACE_FRAMES) {
            unsigned n=sf8_trace_count;
            sf8_trace[n].sequence=seq;
            sf8_trace[n].milliseconds=now;
            sf8_trace[n].frame=frame;
            sf8_debug_trace[n].algorithm=diagnostic.algorithm;
            sf8_debug_trace[n].result=diagnostic.result;
            sf8_trace_count=n+1U;
            if (sf8_trace_count==SF8_TRACE_FRAMES) {
                sf8_trace_arm=0U;
                Xil_DCacheFlushRange((INTPTR)sf8_trace,sizeof(sf8_trace));
                Xil_DCacheFlushRange((INTPTR)sf8_debug_trace,sizeof(sf8_debug_trace));
            }
            Xil_DCacheFlushRange((INTPTR)&sf8_trace_count,sizeof(sf8_trace_count));
            Xil_DCacheFlushRange((INTPTR)&sf8_trace_arm,sizeof(sf8_trace_arm));
        }
        frame_at=now;
        ++diagnostic.frames_processed;
    } else { wr(0x60U,0U); requested=0; }
}

void doa_capture_diagnostics(DoaDiagnostics *out)
{
    *out=diagnostic;
    out->age_ms=(uint32_t)(uptime_millis()-frame_at);
    out->fresh=out->ready && out->frames_processed!=0U && out->age_ms<=STALE_MS;
    if (!out->fresh) memset(&out->result,0,sizeof(out->result));
}

void doa_capture_snapshot(SimSnapshot *out)
{
    DoaDiagnostics d;
    memset(out,0,sizeof(*out));
    doa_capture_diagnostics(&d);
    if (!d.fresh) return;
    memcpy(out->spectrum,d.result.spectrum,sizeof(out->spectrum));
    if (d.result.valid) {
        out->source_count=d.result.source_count;
        for (unsigned i=0; i<out->source_count && i<SIM_SOURCE_COUNT; ++i) {
            out->sources[i].source_id=d.result.sources[i].source_id;
            out->sources[i].angle_deg=d.result.sources[i].angle_deg;
            out->sources[i].strength=d.result.sources[i].strength;
        }
    }
}

const char *doa_capture_state(void)
{
    DoaDiagnostics d;
    doa_capture_diagnostics(&d);
    if (!d.ready) return "array_unavailable";
    if (!d.fresh) return "waiting_for_samples";
    return d.result.valid ? "tracking" : "no_reliable_source";
}

/* Main-loop command: no debugger cache races, no application reset. */
int doa_capture_trace_arm(void)
{
    if (!diagnostic.ready || sf8_trace_arm) return 0;
    sf8_trace_count=0U;sf8_trace_arm=1U;
    Xil_DCacheFlushRange((INTPTR)&sf8_trace_count,sizeof(sf8_trace_count));
    Xil_DCacheFlushRange((INTPTR)&sf8_trace_arm,sizeof(sf8_trace_arm));
    return 1;
}
unsigned doa_capture_trace_count(void) { return sf8_trace_count; }
