#ifndef DHCP_SERVER_H
#define DHCP_SERVER_H

#include "lwip/netif.h"
#include <stdint.h>

/* One direct Ethernet client, with the board fixed at 192.168.77.10. */
int dhcp_server_start(struct netif *netif);
extern volatile uint32_t dhcp_diag[8];

#endif
