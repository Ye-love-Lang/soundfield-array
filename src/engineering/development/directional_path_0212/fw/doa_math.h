#ifndef DOA_MATH_H
#define DOA_MATH_H
#include <stdint.h>

#define DOA_CHANNELS 8U
#define DOA_PAIRS 28U
#define DOA_MAX_LAG 18
#define DOA_LAGS 37U
#define DOA_FRAME_SAMPLES 1024U
#define DOA_DISPLAY_POINTS 72U
/* PCB acoustic centres, viewed from the microphone side. 0=right, 90=up. */
#define DOA_RADIUS_MM 60.0
#define DOA_SAMPLE_RATE 48000.0
#define DOA_SOUND_SPEED_MM_S 343000.0
/* Eight positions at 45-degree intervals; search the complete circle. */
#define DOA_SECTOR_FIRST 0
#define DOA_SECTOR_LAST 359
#define DOA_SCAN_POINTS (DOA_SECTOR_LAST-DOA_SECTOR_FIRST+1)
#define DOA_MIN_RMS 2.0
#define DOA_MIN_COHERENCE 0.20
#define DOA_MIN_PEAK_COHERENCE 0.15
#define DOA_MIN_PROMINENCE 0.025
#define DOA_MAX_SOURCES 4U
#define DOA_PEAK_SEPARATION_DEG 35U
#define DOA_PROMINENCE_WINDOW_DEG 35U
#define DOA_CONFIRM_FRAMES 3U

typedef struct {
    uint32_t source_id;
    uint16_t angle_deg, strength;
} DoaSource;

typedef struct {
    int64_t correlation[DOA_PAIRS][DOA_LAGS];
    uint64_t energy[DOA_CHANNELS];
} DoaFrame;

typedef struct {
    uint16_t spectrum[DOA_DISPLAY_POINTS];
    uint16_t angle_deg, strength, coherence_milli, prominence_milli;
    uint16_t rms[DOA_CHANNELS];
    uint8_t active_mask, valid;
    uint8_t source_count;
    DoaSource sources[DOA_MAX_SOURCES];
} DoaResult;

/* Diagnostic state from exactly one processed frame; not an App ABI. */
typedef struct { uint32_t angle; int32_t level, prominence; } DoaPeakDebug;
typedef struct { uint32_t id,angle,recent,misses,flags; } DoaTrackDebug;
typedef struct {
    uint32_t frames,reason,resets[5],expired;
    uint32_t stable_count,instant_count,candidate_count,evidence_mask;
    DoaPeakDebug stable[4],instant[4],candidates[4];
    DoaTrackDebug tracks[4];
} DoaDebug;
void doa_math_debug(DoaDebug *out);
void doa_math_init(void);
void doa_math_reset(void);
void doa_math_process(const DoaFrame *frame, DoaResult *result);
/* Shared by host tests and geometry documentation. */
double doa_expected_lag(unsigned pair, double angle_deg);
#endif
