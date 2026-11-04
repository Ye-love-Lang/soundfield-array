#include "audio_stream.h"
#include "uptime.h"
#include "lwip/tcp.h"
#include "lwip/pbuf.h"
#include <stdint.h>

#define AUDIO_PORT 9000U
#define CHUNK_SAMPLES 480U
#define CHUNK_BYTES (CHUNK_SAMPLES * 2U)
/* About 680 ms of PCM. A full ring is a recording error, not a license to
 * silently discard samples from a phone WAV that has no sequence numbers. */
#define RING_SAMPLES 32768U

static struct tcp_pcb *listener, *client;
static uint8_t pcm[CHUNK_BYTES];
static uint32_t next_due_ms;
static int16_t ring[RING_SAMPLES];
static unsigned ring_read, ring_write, ring_count;
static uint32_t ring_overflows;
static int active;

static void on_error(void *arg, err_t err)
{
    (void)arg; (void)err;
    client = NULL;
}

static err_t on_receive(void *arg, struct tcp_pcb *pcb,
                        struct pbuf *p, err_t err)
{
    (void)arg;
    if (p == NULL) {
        if (client == pcb) client = NULL;
        if (tcp_close(pcb) != ERR_OK) {
            tcp_abort(pcb);
            return ERR_ABRT;
        }
        return ERR_OK;
    }
    if (err != ERR_OK) return err;
    tcp_recved(pcb, p->tot_len);
    pbuf_free(p); /* The audio connection is downlink only. */
    return ERR_OK;
}

static err_t on_accept(void *arg, struct tcp_pcb *pcb, err_t err)
{
    (void)arg;
    if (err != ERR_OK) return err;
    if (!active || client != NULL) {
        tcp_abort(pcb);
        return ERR_ABRT;
    }
    client = pcb;
    ring_read = ring_write = ring_count = 0U;
    next_due_ms = uptime_millis();
    tcp_arg(pcb, NULL);
    tcp_recv(pcb, on_receive);
    tcp_err(pcb, on_error);
    tcp_nagle_disable(pcb);
    return ERR_OK;
}

int audio_stream_init(void)
{
    if (listener != NULL) return 1;
    listener = tcp_new();
    if (listener == NULL) return 0;
    if (tcp_bind(listener, IP_ADDR_ANY, AUDIO_PORT) != ERR_OK) {
        tcp_close(listener);
        listener = NULL;
        return 0;
    }
    listener = tcp_listen(listener);
    if (listener == NULL) return 0;
    tcp_accept(listener, on_accept);
    return 1;
}

int audio_stream_start(void)
{
    if (listener == NULL) return 0;
    active = 1;
    return 1;
}

void audio_stream_stop(void)
{
    active = 0;
    if (client != NULL) {
        struct tcp_pcb *pcb = client;
        client = NULL;
        tcp_recv(pcb, NULL);
        tcp_err(pcb, NULL);
        tcp_abort(pcb);
    }
}

int audio_stream_is_active(void) { return active; }
uint32_t audio_stream_overflows(void) { return ring_overflows; }

void audio_stream_push_sample(int16_t sample)
{
    if (!active || client == NULL) return;
    if (ring_count == RING_SAMPLES) {
        ++ring_overflows;
        audio_stream_stop();
        return;
    }
    ring[ring_write] = sample;
    ring_write = (ring_write + 1U) % RING_SAMPLES;
    ++ring_count;
}

void audio_stream_poll(void)
{
    uint32_t now;
    unsigned n;
    if (!active || client == NULL) return;
    now = uptime_millis();
    if ((int32_t)(now - next_due_ms) < 0) return;
    if (tcp_sndbuf(client) < CHUNK_BYTES || ring_count < CHUNK_SAMPLES) return;
    for (n = 0U; n < CHUNK_SAMPLES; ++n) {
        uint16_t sample = (uint16_t)ring[(ring_read + n) % RING_SAMPLES];
        pcm[2U * n] = (uint8_t)sample;
        pcm[2U * n + 1U] = (uint8_t)(sample >> 8);
    }
    if (tcp_write(client, pcm, CHUNK_BYTES, TCP_WRITE_FLAG_COPY) != ERR_OK)
        return;
    ring_read = (ring_read + CHUNK_SAMPLES) % RING_SAMPLES;
    ring_count -= CHUNK_SAMPLES;
    (void)tcp_output(client);
    next_due_ms += 10U;
    /* After a long network stall, resume at real-time pacing. */
    if ((uint32_t)(now - next_due_ms) > 100U) next_due_ms = now + 10U;
}
