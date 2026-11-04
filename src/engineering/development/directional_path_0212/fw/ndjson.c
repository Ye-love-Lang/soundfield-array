#include "ndjson.h"

void ndjson_init(NdjsonRx *rx)
{
    rx->used = 0U;
    rx->dropping = 0;
}

int ndjson_feed(NdjsonRx *rx, uint8_t ch, const char **frame, size_t *length)
{
    if (ch == (uint8_t)'\n') {
        int was_dropping = rx->dropping;
        size_t n = rx->used;
        rx->used = 0U;
        rx->dropping = 0;
        if (was_dropping) return -1;
        if (n != 0U && rx->bytes[n - 1U] == '\r') n--; /* accept CRLF */
        rx->bytes[n] = '\0';
        *frame = rx->bytes;
        *length = n;
        return 1;
    }
    if (rx->dropping) return 0;
    if (rx->used == NDJSON_MAX_BYTES) {
        rx->dropping = 1; /* discard until next LF to resynchronize */
        return 0;
    }
    rx->bytes[rx->used++] = (char)ch;
    return 0;
}
