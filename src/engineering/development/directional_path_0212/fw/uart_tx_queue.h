#ifndef UART_TX_QUEUE_H
#define UART_TX_QUEUE_H
#include <stddef.h>
#include <stdint.h>
#define UART_TX_QUEUE_CAP 16384U

typedef struct {
    uint8_t buffer[UART_TX_QUEUE_CAP];
    size_t rd, wr, count;
} UartTxQueue;

void uart_tx_queue_init(UartTxQueue *q);
/* Returns 1 if entire response + LF was queued, 0 if not enough room. */
int uart_tx_queue_append_line(UartTxQueue *q, const char *data, size_t n);
/* Sends up to 'limit' bytes without waiting for the UART TX FIFO. */
void uart_tx_queue_pump(UartTxQueue *q, unsigned limit);
#endif
