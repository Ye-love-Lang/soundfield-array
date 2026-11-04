#ifndef DOA_CAPTURE_H
#define DOA_CAPTURE_H
#include "doa_math.h"
#include "sim_data.h"
typedef struct {
    uint32_t sequence, frames_processed, overruns, sample_count, age_ms;
    int ready, fresh;
    DoaResult result;
    DoaDebug algorithm;
} DoaDiagnostics;
int doa_capture_init(void);
int doa_capture_trace_arm(void);
unsigned doa_capture_trace_count(void);
void doa_capture_poll(void);
void doa_capture_snapshot(SimSnapshot *out);
void doa_capture_diagnostics(DoaDiagnostics *out);
const char *doa_capture_state(void);
#endif
