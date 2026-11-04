#ifndef UPTIME_H
#define UPTIME_H
#include <stdint.h>
void uptime_init(void);
/* Milliseconds since application initialization, wraps after 49.7 days. */
uint32_t uptime_millis(void);
#ifdef HOST_TEST
void uptime_test_set(uint32_t value);
#endif
#endif
