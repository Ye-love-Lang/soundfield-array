#ifndef SPATIAL_CONTROL_H
#define SPATIAL_CONTROL_H
#include <stdint.h>
typedef struct {uint32_t id;uint16_t angle,strength;} SFSource;
typedef uint32_t (*SFRead)(unsigned offset);
typedef void (*SFWrite)(unsigned offset,uint32_t value);
typedef struct {
 uint32_t supported,phase,applied_target,applied_interference,updates,failures,max_poll_ms;
 uint32_t rejected_designs,last_design_error;
} SFControlStatus;
int sf_control_init(SFRead read_fn,SFWrite write_fn);
void sf_control_poll(uint32_t now,unsigned target,uint32_t sequence,int fresh,const SFSource *sources,unsigned count);
void sf_control_status(SFControlStatus *out);
void sf_control_timing(uint32_t elapsed_ms);
#endif
