#ifndef MIC_CAPTURE_H
#define MIC_CAPTURE_H
#include <stdint.h>
void mic_capture_audio_context(void);

/* Largest integer dB gain representable by the PL's unsigned Q16.16 register. */
#define MIC_GAIN_MAX_DB 96U

int mic_capture_init(void);
void mic_capture_poll(void);
int mic_capture_ready(void);
int mic_capture_beamforming_ready(void);
int mic_capture_set_direction(uint32_t angle_deg);
uint32_t mic_capture_effective_gain(void);
uint32_t mic_capture_limited_samples(void);
uint32_t mic_capture_beam_overruns(void);
int mic_capture_set_cutoff(uint32_t hz);
uint32_t mic_capture_cutoff(void);
int mic_capture_set_filter_enabled(int enabled);
int mic_capture_filter_enabled(void);
int mic_capture_set_gain(uint32_t db);
uint32_t mic_capture_gain(void);
uint32_t mic_capture_samples(void);
uint32_t mic_capture_fifo_overflows(void);
uint32_t mic_capture_filter_overruns(void);

#endif
