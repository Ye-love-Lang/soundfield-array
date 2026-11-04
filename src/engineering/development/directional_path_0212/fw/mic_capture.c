#include "mic_capture.h"
#include "doa_math.h"
#include "audio_stream.h"
#include "audio_capture.h"
#include "spatial_runtime.h"
#include "xil_io.h"
#include <math.h>

#define MIC_BASE ((UINTPTR)0x43C00000U)
#define MIC_STATUS 0x00U
#define MIC_DATA 0x04U
#define MIC_COEFF_INDEX 0x08U
#define MIC_COEFF_VALUE 0x0CU
#define MIC_GAIN_Q16_16 0x10U
#define MIC_FILTER_ENABLE 0x14U
#define MIC_COMMIT 0x1CU
#define MIC_SAMPLES 0x20U
#define MIC_FIFO_OVERFLOWS 0x24U
#define MIC_FILTER_OVERRUNS 0x28U
#define MIC_SIGNATURE 0x3CU
#define MIC_MAGIC 0x4D494335U

static int ready;
static uint32_t cutoff_hz = 0U;
static uint32_t gain_db = 0U;

static uint32_t read_reg(unsigned offset) { return Xil_In32(MIC_BASE + offset); }
static void write_reg(unsigned offset, uint32_t value)
{
    Xil_Out32(MIC_BASE + offset, value);
}

int mic_capture_init(void)
{
    ready = read_reg(MIC_SIGNATURE) == MIC_MAGIC;
    if (ready) {
        (void)mic_capture_set_filter_enabled(0);
        (void)mic_capture_set_gain(0U);
        (void)mic_capture_set_cutoff(8000U);
    }
    if (ready && mic_capture_beamforming_ready()) (void)mic_capture_set_direction(0U);
    return ready;
}

int mic_capture_ready(void) { return ready; }

void mic_capture_audio_context(void){
    SFControlStatus spatial;spatial_runtime_status(&spatial);
    ACContext context={spatial.applied_target,spatial.applied_interference,
        spatial.updates,spatial.phase,spatial_runtime_active(),spatial.failures,
        gain_db,cutoff_hz,(uint32_t)mic_capture_filter_enabled(),
        mic_capture_fifo_overflows(),mic_capture_beam_overruns(),
        mic_capture_limited_samples()};
    ac_context(&context);
}
/* A maximum of 512 AXI reads per service call leaves time for lwIP and UART. */
void mic_capture_poll(void)
{
    unsigned i;
    if (!ready) return;
    if(ac_running())mic_capture_audio_context();
    for (i = 0U; i < 512U; ++i) {
        int16_t sample;
        if ((read_reg(MIC_STATUS) & 1U) != 0U) break;
        sample = (int16_t)read_reg(MIC_DATA);
        ac_push(sample);
        audio_stream_push_sample(sample);
    }
    if(ac_running())mic_capture_audio_context();
}

int mic_capture_set_cutoff(uint32_t hz)
{
    double k, k2;
    unsigned section, term;
    const double scale = 536870912.0; /* Q2.29 */
    if (!ready || hz < 100U || hz > 10000U) return 0;
    k = tan(3.14159265358979323846 * (double)hz / 48000.0);
    k2 = k * k;
    /* Low-Q sections first. Each section has unity DC gain. */
    for (section = 0U; section < 18U; ++section) {
        double damping = 2.0 * sin((double)(35U - 2U * section) *
                                     3.14159265358979323846 / 72.0);
        double norm = 1.0 / (1.0 + damping * k + k2);
        double b0 = k2 * norm;
        double coefficients[5];
        coefficients[0] = b0;
        coefficients[1] = 2.0 * b0;
        coefficients[2] = b0;
        coefficients[3] = 2.0 * (k2 - 1.0) * norm;
        coefficients[4] = (1.0 - damping * k + k2) * norm;
        for (term = 0U; term < 5U; ++term) {
            write_reg(MIC_COEFF_INDEX, 5U * section + term);
            write_reg(MIC_COEFF_VALUE,
                      (uint32_t)(int32_t)llround(coefficients[term] * scale));
        }
    }
    write_reg(MIC_COMMIT, 1U);
    cutoff_hz = hz;
    return 1;
}

uint32_t mic_capture_cutoff(void) { return cutoff_hz; }

int mic_capture_set_filter_enabled(int enabled)
{
    if (!ready || (enabled != 0 && enabled != 1)) return 0;
    write_reg(MIC_FILTER_ENABLE, (uint32_t)enabled);
    return (int)(read_reg(MIC_FILTER_ENABLE) & 1U) == enabled;
}

int mic_capture_filter_enabled(void)
{
    return ready ? (int)(read_reg(MIC_FILTER_ENABLE) & 1U) : 0;
}

int mic_capture_set_gain(uint32_t db)
{
    uint64_t gain_q16_16;
    if (!ready || db > MIC_GAIN_MAX_DB) return 0;
    gain_q16_16 = (uint64_t)llround(pow(10.0, (double)db / 20.0) * 65536.0);
    if (gain_q16_16 > UINT32_MAX) return 0;
    write_reg(MIC_GAIN_Q16_16, (uint32_t)gain_q16_16);
    gain_db = db;
    return 1;
}

uint32_t mic_capture_gain(void) { return gain_db; }

uint32_t mic_capture_samples(void) { return ready ? read_reg(MIC_SAMPLES) : 0U; }
uint32_t mic_capture_fifo_overflows(void)
{
    return ready ? read_reg(MIC_FIFO_OVERFLOWS) : 0U;
}
uint32_t mic_capture_filter_overruns(void)
{
    return ready ? read_reg(MIC_FILTER_OVERRUNS) : 0U;
}

int mic_capture_beamforming_ready(void)
{
    return ready && read_reg(0x7CU)==0x42464D31U;
}

int mic_capture_set_direction(uint32_t angle_deg)
{
    if (!mic_capture_beamforming_ready() || angle_deg>359U) return 0;
    for (unsigned i=0;i<8U;++i) {
        /* Arrival is -r.dot(direction)/c; delay the earlier microphone more.
         * The common radius offset makes every delay causal. */
        double a=(90.0+45.0*i-angle_deg)*3.14159265358979323846/180.0;
        double samples=DOA_RADIUS_MM*(1.0+cos(a))*DOA_SAMPLE_RATE/DOA_SOUND_SPEED_MM_S;
        uint32_t q8=(uint32_t)llround(samples*256.0);
        if(q8>31U*256U) return 0;
        write_reg(0x70U,i);write_reg(0x74U,q8);
    }
    write_reg(0x78U,3U); /* Enable + atomic coefficient commit. */
    return (read_reg(0x78U)&1U)!=0U;
}

uint32_t mic_capture_effective_gain(void)
{ return ready && read_reg(0x8CU)==0x4C494D31U ? read_reg(0x88U) : 0U; }
uint32_t mic_capture_limited_samples(void)
{ return ready && read_reg(0x8CU)==0x4C494D31U ? read_reg(0x84U) : 0U; }
uint32_t mic_capture_beam_overruns(void)
{ return mic_capture_beamforming_ready() ? read_reg(0x80U) : 0U; }
