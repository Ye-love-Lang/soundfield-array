#ifndef NDJSON_H
#define NDJSON_H
#include <stddef.h>
#include <stdint.h>

#define NDJSON_MAX_BYTES 2048U

typedef struct {
    char bytes[NDJSON_MAX_BYTES + 1U];
    size_t used;
    int dropping;
} NdjsonRx;

/* 0: no complete line; 1: frame ready; -1: oversized line discarded.
 * Frame is valid only until the next feed call. No UART dependency. */
int ndjson_feed(NdjsonRx *rx, uint8_t ch, const char **frame, size_t *length);
void ndjson_init(NdjsonRx *rx);

#endif
