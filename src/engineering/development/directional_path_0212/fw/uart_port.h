#ifndef UART_PORT_H
#define UART_PORT_H
#include "xil_types.h"
#include "xstatus.h"

int uart_port_init(void);
int uart_port_try_read(u8 *value);
/* New for phase 2: non-blocking TX for a software TX queue. */
int uart_port_try_write(u8 value);
void uart_port_write_byte(u8 value);
void uart_port_write_string(const char *str);
#endif
