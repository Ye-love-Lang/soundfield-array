#ifndef SPATIAL_RUNTIME_H
#define SPATIAL_RUNTIME_H
#include "spatial_control.h"
int spatial_runtime_init(void);
void spatial_runtime_poll(void);
void spatial_runtime_status(SFControlStatus *out);
unsigned spatial_runtime_active(void);
int spatial_runtime_stream_ready(void);
#endif
