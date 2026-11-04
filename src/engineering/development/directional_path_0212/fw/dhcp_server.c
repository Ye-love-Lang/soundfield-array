#include "dhcp_server.h"

#include "lwip/udp.h"
#include "lwip/pbuf.h"
#include "lwip/ip4_addr.h"
#include <stdint.h>
#include <string.h>

#define DHCP_SERVER_PORT 67U
#define DHCP_CLIENT_PORT 68U
#define DHCP_PACKET_MIN 240U
#define DHCP_PACKET_MAX 576U
#define DHCP_REPLY_SIZE 300U
#define DHCP_DISCOVER 1U
#define DHCP_OFFER 2U
#define DHCP_REQUEST 3U
#define DHCP_ACK 5U
#define DHCP_NAK 6U

/* This server is intended for a single phone connected directly to the board.
 * A fixed offer avoids lease state and keeps the app's board IP unchanged. */
static const unsigned char server_ip[4] = {192U, 168U, 77U, 10U};
static const unsigned char client_ip[4] = {192U, 168U, 77U, 2U};
static struct udp_pcb *server_pcb;
static struct netif *server_netif;
/* JTAG-only diagnostics: RX, valid, DISCOVER, REQUEST, sent, send errors,
 * last RX length, and last DHCP message type. No UART protocol changes. */
volatile uint32_t dhcp_diag[8];

static int same_ip(const unsigned char *a, const unsigned char *b)
{
    return memcmp(a, b, 4U) == 0;
}

static void reply_to_client(const unsigned char *request, unsigned type)
{
    unsigned char packet[DHCP_REPLY_SIZE];
    unsigned i = DHCP_PACKET_MIN;
    ip_addr_t destination;
    struct pbuf *p;

    memset(packet, 0, sizeof(packet));
    packet[0] = 2U; /* BOOTREPLY */
    packet[1] = 1U; /* Ethernet */
    packet[2] = 6U;
    memcpy(packet + 4U, request + 4U, 4U);   /* transaction ID */
    memcpy(packet + 10U, request + 10U, 2U); /* client flags */
    memcpy(packet + 28U, request + 28U, 16U); /* client hardware address */
    if (type != DHCP_NAK) memcpy(packet + 16U, client_ip, 4U); /* yiaddr */
    memcpy(packet + 20U, server_ip, 4U); /* siaddr */
    packet[236] = 99U; packet[237] = 130U;
    packet[238] = 83U; packet[239] = 99U;

    packet[i++] = 53U; packet[i++] = 1U; packet[i++] = (unsigned char)type;
    packet[i++] = 54U; packet[i++] = 4U;
    memcpy(packet + i, server_ip, 4U); i += 4U;
    if (type != DHCP_NAK) {
        packet[i++] = 51U; packet[i++] = 4U; /* one-hour lease */
        packet[i++] = 0U; packet[i++] = 0U;
        packet[i++] = 14U; packet[i++] = 16U;
        packet[i++] = 1U; packet[i++] = 4U; /* subnet mask */
        packet[i++] = 255U; packet[i++] = 255U;
        packet[i++] = 255U; packet[i++] = 0U;
        packet[i++] = 3U; packet[i++] = 4U; /* board as gateway */
        memcpy(packet + i, server_ip, 4U); i += 4U;
        packet[i++] = 6U; packet[i++] = 4U; /* DNS, for network registration */
        memcpy(packet + i, server_ip, 4U); i += 4U;
    }
    packet[i] = 255U;
    p = pbuf_alloc(PBUF_TRANSPORT, DHCP_REPLY_SIZE, PBUF_RAM);
    if (p == NULL) {
        ++dhcp_diag[5];
        return;
    }
    if (pbuf_take(p, packet, DHCP_REPLY_SIZE) == ERR_OK) {
        /* A new client cannot receive unicast until it configures its address. */
        IP4_ADDR(&destination, 255, 255, 255, 255);
        if (type == DHCP_ACK && same_ip(request + 12U, client_ip) &&
            (request[10] & 0x80U) == 0U)
            IP4_ADDR(&destination, 192, 168, 77, 2);
        if (udp_sendto_if(server_pcb, p, &destination,
                          DHCP_CLIENT_PORT, server_netif) == ERR_OK)
            ++dhcp_diag[4];
        else
            ++dhcp_diag[5];
    }
    pbuf_free(p);
}

static void on_dhcp(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                    const ip_addr_t *address, u16_t port)
{
    unsigned char request[DHCP_PACKET_MAX];
    unsigned message_type = 0U;
    unsigned char requested_ip[4] = {0U, 0U, 0U, 0U};
    unsigned char selected_server[4] = {0U, 0U, 0U, 0U};
    int has_requested_ip = 0, has_server = 0;
    unsigned i;
    (void)arg; (void)pcb; (void)address;
    if (p == NULL) return;
    ++dhcp_diag[0];
    dhcp_diag[6] = p->tot_len;
    if (port != DHCP_CLIENT_PORT || p->tot_len < DHCP_PACKET_MIN ||
        p->tot_len > DHCP_PACKET_MAX ||
        pbuf_copy_partial(p, request, p->tot_len, 0U) != p->tot_len)
        goto done;
    if (request[0] != 1U || request[1] != 1U || request[2] != 6U ||
        request[236] != 99U || request[237] != 130U ||
        request[238] != 83U || request[239] != 99U) goto done;
    ++dhcp_diag[1];

    /* Parse options with bounds checks; accept padding and a missing END. */
    for (i = DHCP_PACKET_MIN; i < p->tot_len;) {
        unsigned tag = request[i++], length;
        if (tag == 0U) continue;
        if (tag == 255U) break;
        if (i >= p->tot_len) goto done;
        length = request[i++];
        if (length > p->tot_len - i) goto done;
        if (tag == 53U && length == 1U) message_type = request[i];
        if (tag == 50U && length == 4U) {
            memcpy(requested_ip, request + i, 4U);
            has_requested_ip = 1;
        }
        if (tag == 54U && length == 4U) {
            memcpy(selected_server, request + i, 4U);
            has_server = 1;
        }
        i += length;
    }
    if (message_type == DHCP_DISCOVER) {
        ++dhcp_diag[2];
        dhcp_diag[7] = message_type;
        reply_to_client(request, DHCP_OFFER);
    } else if (message_type == DHCP_REQUEST) {
        ++dhcp_diag[3];
        dhcp_diag[7] = message_type;
        /* A request for another server is not ours to answer. */
        if (has_server && !same_ip(selected_server, server_ip)) goto done;
        if ((has_requested_ip && !same_ip(requested_ip, client_ip)) ||
            (!has_requested_ip && !same_ip(request + 12U, client_ip)))
            reply_to_client(request, DHCP_NAK);
        else
            reply_to_client(request, DHCP_ACK);
    }
done:
    pbuf_free(p);
}

int dhcp_server_start(struct netif *netif)
{
    server_netif = netif;
    server_pcb = udp_new();
    if (server_pcb == NULL) return 0;
    if (udp_bind(server_pcb, IP_ADDR_ANY, DHCP_SERVER_PORT) != ERR_OK) {
        udp_remove(server_pcb);
        server_pcb = NULL;
        return 0;
    }
    udp_bind_netif(server_pcb, netif);
    udp_recv(server_pcb, on_dhcp, NULL);
    return 1;
}
