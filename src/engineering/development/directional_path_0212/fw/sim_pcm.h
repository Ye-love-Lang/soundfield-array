#ifndef SIM_PCM_H
#define SIM_PCM_H
#include <stdint.h>

/* Deterministic 1 kHz, 48 kHz, mono 16-bit simulated source. */
int16_t sim_pcm_sample(uint32_t sample_index);

#endif
