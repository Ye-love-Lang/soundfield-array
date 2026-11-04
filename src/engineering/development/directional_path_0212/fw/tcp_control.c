#include "tcp_control.h"
#include "ndjson.h"
#include "protocol.h"
#include "audio_stream.h"
#include "lwip/tcp.h"
#include "lwip/pbuf.h"
#include <string.h>

#define CONTROL_PORT 7000U
#define CLIENT_COUNT 4U
#define RING_CAP 8192U

typedef struct {
    struct tcp_pcb *pcb;
    NdjsonRx parser;
    unsigned char rx[RING_CAP], tx[RING_CAP];
    unsigned rx_head, rx_tail, rx_used;
    unsigned tx_head, tx_tail, tx_used;
    int peer_closed;
} Client;

static Client clients[CLIENT_COUNT];
static struct tcp_pcb *listener;
/* The RAW API executes callbacks in the same thread as network_poll. */
static char reply[PROTOCOL_RESPONSE_CAP];

static void release(Client *c)
{
    c->pcb = NULL;
    c->rx_head = c->rx_tail = c->rx_used = 0U;
    c->tx_head = c->tx_tail = c->tx_used = 0U;
    c->peer_closed = 0;
    ndjson_init(&c->parser);
}

static void stop_audio_if_no_control(void)
{
    unsigned i;
    for (i = 0U; i < CLIENT_COUNT; ++i)
        if (clients[i].pcb != NULL) return;
    audio_stream_stop();
}

static void on_error(void *arg, err_t err)
{
    (void)err;
    release((Client *)arg); /* lwIP already destroyed the pcb. */
    stop_audio_if_no_control();
}

static err_t on_receive(void *arg, struct tcp_pcb *pcb,
                        struct pbuf *p, err_t err)
{
    Client *c = (Client *)arg;
    unsigned left, offset = 0U;
    if (c == NULL || c->pcb != pcb) {
        if (p != NULL) pbuf_free(p);
        return ERR_OK;
    }
    if (p == NULL) {
        c->peer_closed = 1;
        return ERR_OK;
    }
    if (err != ERR_OK) return err;
    if (p->tot_len > RING_CAP - c->rx_used) return ERR_MEM;
    left = p->tot_len;
    while (left != 0U) {
        unsigned chunk = RING_CAP - c->rx_head;
        if (chunk > left) chunk = left;
        (void)pbuf_copy_partial(p, &c->rx[c->rx_head], chunk, offset);
        c->rx_head = (c->rx_head + chunk) % RING_CAP;
        c->rx_used += chunk;
        offset += chunk;
        left -= chunk;
    }
    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);
    return ERR_OK;
}

static err_t on_sent(void *arg, struct tcp_pcb *pcb, u16_t len)
{
    (void)arg; (void)pcb; (void)len;
    return ERR_OK;
}

static err_t on_poll(void *arg, struct tcp_pcb *pcb)
{
    (void)arg; (void)pcb;
    return ERR_OK;
}

static err_t on_accept(void *arg, struct tcp_pcb *pcb, err_t err)
{
    unsigned i;
    (void)arg;
    if (err != ERR_OK) return err;
    for (i = 0U; i < CLIENT_COUNT; ++i) {
        Client *c = &clients[i];
        if (c->pcb != NULL) continue;
        release(c);
        c->pcb = pcb;
        tcp_arg(pcb, c);
        tcp_recv(pcb, on_receive);
        tcp_sent(pcb, on_sent);
        tcp_err(pcb, on_error);
        tcp_poll(pcb, on_poll, 2);
        return ERR_OK;
    }
    tcp_abort(pcb);
    return ERR_ABRT;
}

int tcp_control_start(void)
{
    err_t err;
    listener = tcp_new();
    if (listener == NULL) return 0;
    err = tcp_bind(listener, IP_ADDR_ANY, CONTROL_PORT);
    if (err != ERR_OK) {
        tcp_close(listener);
        listener = NULL;
        return 0;
    }
    listener = tcp_listen(listener);
    if (listener == NULL) return 0;
    tcp_accept(listener, on_accept);
    return 1;
}

static void append_response(Client *c, const char *data, unsigned len)
{
    unsigned i;
    for (i = 0U; i < len; ++i) {
        c->tx[c->tx_head] = (unsigned char)data[i];
        c->tx_head = (c->tx_head + 1U) % RING_CAP;
    }
    c->tx[c->tx_head] = '\n';
    c->tx_head = (c->tx_head + 1U) % RING_CAP;
    c->tx_used += len + 1U;
}

static void pump_tx(Client *c)
{
    int wrote = 0;
    while (c->tx_used != 0U && tcp_sndbuf(c->pcb) != 0U) {
        unsigned chunk = RING_CAP - c->tx_tail;
        u16_t free_bytes = tcp_sndbuf(c->pcb);
        if (chunk > c->tx_used) chunk = c->tx_used;
        if (chunk > free_bytes) chunk = free_bytes;
        if (tcp_write(c->pcb, &c->tx[c->tx_tail], (u16_t)chunk,
                      TCP_WRITE_FLAG_COPY) != ERR_OK) break;
        c->tx_tail = (c->tx_tail + chunk) % RING_CAP;
        c->tx_used -= chunk;
        wrote = 1;
    }
    if (wrote) (void)tcp_output(c->pcb);
}

void tcp_control_poll(void)
{
    unsigned i;
    for (i = 0U; i < CLIENT_COUNT; ++i) {
        Client *c = &clients[i];
        unsigned budget = 256U;
        if (c->pcb == NULL) continue;
        pump_tx(c);
        while (c->rx_used != 0U && budget-- != 0U &&
               RING_CAP - c->tx_used >= PROTOCOL_RESPONSE_CAP + 1U) {
            const char *frame = NULL;
            size_t length = 0U, reply_len = 0U;
            int complete = ndjson_feed(&c->parser, c->rx[c->rx_tail],
                                       &frame, &length);
            c->rx_tail = (c->rx_tail + 1U) % RING_CAP;
            --c->rx_used;
            if (complete == 1)
                reply_len = protocol_handle_transport(frame, length,
                    PROTOCOL_TRANSPORT_TCP_CONTROL, reply, sizeof(reply));
            else if (complete == -1)
                reply_len = protocol_frame_too_long(reply, sizeof(reply));
            if (reply_len != 0U) append_response(c, reply, (unsigned)reply_len);
        }
        pump_tx(c);
        if (c->peer_closed && c->rx_used == 0U && c->tx_used == 0U) {
            struct tcp_pcb *pcb = c->pcb;
            if (tcp_close(pcb) == ERR_OK) {
                release(c);
                stop_audio_if_no_control();
            }
        }
    }
}
