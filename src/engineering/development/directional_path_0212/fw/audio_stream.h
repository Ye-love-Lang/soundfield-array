#ifndef AUDIO_STREAM_H
#define AUDIO_STREAM_H
#include <stdint.h>

int audio_stream_init(void);
int audio_stream_start(void);
void audio_stream_stop(void);
int audio_stream_is_active(void);
uint32_t audio_stream_overflows(void);
void audio_stream_push_sample(int16_t sample);
void audio_stream_poll(void);

#endif
