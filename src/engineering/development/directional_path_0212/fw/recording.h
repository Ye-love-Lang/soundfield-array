#ifndef RECORDING_H
#define RECORDING_H
#include <stddef.h>
#include <stdint.h>

#define RECORDING_MAX_SECONDS 20U

int recording_start(void);
void recording_stop(void);
void recording_poll(void);
void recording_push_sample(int16_t sample);
int recording_is_active(void);
int recording_is_ready(void);
const char *recording_id(void);
const char *recording_filename(void);
uint32_t recording_wav_size(void);
const uint8_t *recording_pcm_bytes(void);
uint32_t recording_pcm_size(void);
void recording_wav_header(uint8_t out[44]);
int recording_acquire(void);
void recording_release(void);

#endif
