#ifndef RAW8_H
#define RAW8_H
#include <stdint.h>
typedef struct {uint32_t supported,state,records,errors,clips,generation,first,last,export_ready,export_generation,max_poll_ms,export_first,export_last,export_clips;} Raw8Status;
void raw8_init(void);
void raw8_poll(void);
void raw8_status(Raw8Status *out);
int raw8_export_arm(void);
int raw8_calibrate(unsigned angle);
int raw8_calibrate_path(unsigned angle);
int raw8_read(unsigned offset,unsigned count,int32_t *out);
#endif
