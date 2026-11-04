#include "uart_tx_queue.h"
#include "uart_port.h"

void uart_tx_queue_init(UartTxQueue *q)
{
    q->rd = q->wr = q->count = 0U;
}

int uart_tx_queue_append_line(UartTxQueue *q, const char *data, size_t n)
{
    if (n > UART_TX_QUEUE_CAP - 1U || q->count > UART_TX_QUEUE_CAP - n - 1U) return 0;
    for (size_t i = 0; i < n; ++i) {
        q->buffer[q->wr] = (uint8_t)data[i];
        q->wr = (q->wr + 1U) % UART_TX_QUEUE_CAP;
    }
    q->buffer[q->wr] = (uint8_t)'\n';
    q->wr = (q->wr + 1U) % UART_TX_QUEUE_CAP;
    q->count += n + 1U;
    return 1;
}

void uart_tx_queue_pump(UartTxQueue *q, unsigned limit)
{
    for (unsigned sent = 0; sent < limit && q->count; ++sent) {
        uint8_t ch = q->buffer[q->rd];
        if (!uart_port_try_write(ch)) break;
        q->rd = (q->rd + 1U) % UART_TX_QUEUE_CAP;
        q->count--;
    }
}
