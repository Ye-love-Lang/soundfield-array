#ifndef NETWORK_H
#define NETWORK_H
#include <stdint.h>

/* Returns 1 once GEM and the TCP listener are initialized. */
int network_init(void);
void network_poll(void);
/* Init, link, L2/IPv4/UDP counters, and GEM receive statistics. */
extern volatile uint32_t network_diag[19];
void network_diag_refresh_hw(void);

#endif
