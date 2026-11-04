#ifndef PROTOCOL_H
#define PROTOCOL_H
#include <stddef.h>

#define PROTOCOL_RESPONSE_CAP 1024U

typedef enum {
    PROTOCOL_TRANSPORT_UART1 = 0,
    PROTOCOL_TRANSPORT_TCP_CONTROL = 1,
    PROTOCOL_TRANSPORT_BLUETOOTH_SPP = 2
} ProtocolTransport;

/* Process one JSON document (no trailing newline) and always write JSON. */
size_t protocol_handle_transport(const char *line, size_t length,
                                 ProtocolTransport transport,
                                 char *response, size_t response_capacity);
/* Backwards compatible wrapper for phase-2 UART application. */
size_t protocol_handle(const char *line, size_t length,
                       char *response, size_t response_capacity);
size_t protocol_frame_too_long(char *response, size_t response_capacity);

#endif
