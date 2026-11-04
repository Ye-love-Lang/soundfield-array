#include "uart_port.h"
#include "uart_tx_queue.h"
#include "ndjson.h"
#include "protocol.h"
#include "uptime.h"
#include "app_state.h"
#include "network.h"
#include "mic_capture.h"
#include "doa_capture.h"
#include "spatial_runtime.h"
#include "raw8.h"

/* All buffers static: safe against small embedded thread stacks. */
static NdjsonRx rx;
static UartTxQueue tx;
static char response[PROTOCOL_RESPONSE_CAP];

int main(void)
{
    const char *frame;
    size_t length, response_length;
    u8 byte;

    if (uart_port_init() != XST_SUCCESS) {
        for (;;) { /* debug failure via JTAG; never print on JSON port */ }
    }
    uptime_init();
    app_state_init();
    ndjson_init(&rx);
    uart_tx_queue_init(&tx);
    (void)mic_capture_init();
    (void)doa_capture_init();
    (void)spatial_runtime_init();
    raw8_init();
    (void)network_init();

    /* No startup banner or printf on the JSON control port. */
    for (;;) {
        /* Service incoming UART before draining outgoing responses. */
        if (uart_port_try_read(&byte)) {
            int result = ndjson_feed(&rx, byte, &frame, &length);
            if (result != 0) {
                if (result == -1)
                    response_length = protocol_frame_too_long(response, sizeof(response));
                else
                    response_length = protocol_handle(frame, length, response, sizeof(response));
                if (response_length != 0U) {
                    /* A burst requires bounded TX memory: don't silently drop
                     * an already-created response if that queue gets full. */
                    while (!uart_tx_queue_append_line(&tx, response, response_length)) {
                        uart_tx_queue_pump(&tx, 64U);
                    }
                }
            }
        }
        uart_tx_queue_pump(&tx, 16U);
        network_poll();
        doa_capture_poll();
        spatial_runtime_poll();
        raw8_poll();
    }
}
