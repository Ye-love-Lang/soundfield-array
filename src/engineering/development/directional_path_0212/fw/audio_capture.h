#ifndef AUDIO_CAPTURE_H
#define AUDIO_CAPTURE_H
#include <stdint.h>
#define AC_SAMPLES 32768U
#define AC_DISCARD 1024U
typedef struct {
 uint32_t target, interference, updates, phase, active, failures;
 uint32_t gain, cutoff, filter, fifo, beam_overruns, limited;
} ACContext;
typedef struct {
 uint32_t generation, state, count, valid;
 int64_t sum; uint64_t squares;
 uint32_t peak, rails;
 ACContext context;
} ACStatus;
/* Main-loop only. No interrupt, debugger halt or network dependency. */
void ac_context(const ACContext *context);
int ac_arm(void);
int ac_running(void);
void ac_push(int16_t sample);
void ac_status(ACStatus *status);
int ac_read(unsigned offset,unsigned count,int16_t *out);
#endif
