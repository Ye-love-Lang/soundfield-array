#include "network.h"
#include "tcp_control.h"
#include "audio_stream.h"
#include "dhcp_server.h"
#include "uptime.h"
#include "mic_capture.h"

#include "xparameters.h"
#include "xemacps_hw.h"
#include "netif/xadapter.h"
#include "lwip/init.h"
#include "lwip/tcp.h"
#include "lwip/pbuf.h"

/* lwIP 2.2.0 exposes these timer entry points through its internal header. */
void tcp_fasttmr(void);
void tcp_slowtmr(void);

static struct netif g_netif;
static netif_input_fn g_lwip_input;
static uint32_t g_fast_at, g_slow_at, g_link_at;
static int g_ready;
/* UART diagnostics: init, polls, link, L2 and GEM RX counters, followed by
 * UDP/IPv4 packet details for diagnosing phone DHCP hot-plug attempts. */
volatile uint32_t network_diag[19];

static err_t counting_input(struct pbuf *p, struct netif *netif)
{
    unsigned char eth[64];
    unsigned copy_len;
    ++network_diag[3];
    if (p != NULL && p->tot_len >= 14U) {
        copy_len = p->tot_len < sizeof(eth) ? p->tot_len : sizeof(eth);
        if (pbuf_copy_partial(p, eth, copy_len, 0U) != copy_len)
            return g_lwip_input(p, netif);
        uint32_t type = ((uint32_t)eth[12] << 8) | eth[13];
        network_diag[7] = type;
        network_diag[8] = ((uint32_t)eth[6] << 16) |
                          ((uint32_t)eth[7] << 8) | eth[8];
        network_diag[18] = p->tot_len;
        if (type == 0x0800U) {
            ++network_diag[4];
            if (copy_len >= 34U && (eth[14] >> 4) == 4U) {
                unsigned ip_header_len = (eth[14] & 15U) * 4U;
                network_diag[13] = ((uint32_t)eth[26] << 24) |
                                   ((uint32_t)eth[27] << 16) |
                                   ((uint32_t)eth[28] << 8) | eth[29];
                network_diag[14] = ((uint32_t)eth[30] << 24) |
                                   ((uint32_t)eth[31] << 16) |
                                   ((uint32_t)eth[32] << 8) | eth[33];
                network_diag[17] = eth[23];
                if (eth[23] == 17U && ip_header_len >= 20U &&
                    14U + ip_header_len + 8U <= copy_len) {
                    unsigned udp = 14U + ip_header_len;
                    unsigned dst = ((unsigned)eth[udp + 2U] << 8) |
                                    eth[udp + 3U];
                    network_diag[15] = ((uint32_t)eth[udp] << 8) |
                                       eth[udp + 1U];
                    network_diag[16] = dst;
                    if (dst == 67U) ++network_diag[12];
                }
            }
        }
        if (type == 0x0806U) ++network_diag[5];
        if (type == 0x86DDU) ++network_diag[6];
    }
    return g_lwip_input(p, netif);
}

void network_diag_refresh_hw(void)
{
    network_diag[9] = XEmacPs_ReadReg(XPAR_XEMACPS_0_BASEADDR,
                                     XEMACPS_RXCNT_OFFSET);
    network_diag[10] = XEmacPs_ReadReg(XPAR_XEMACPS_0_BASEADDR,
                                      XEMACPS_RXBROADCNT_OFFSET);
    network_diag[11] = XEmacPs_ReadReg(XPAR_XEMACPS_0_BASEADDR,
                                      XEMACPS_RXFCSCNT_OFFSET);
}

int network_init(void)
{
    ip_addr_t ip, mask, gateway;
    /* Locally administered MAC; use only one board with this address on a LAN. */
    unsigned char mac[6] = {0x02, 0x0A, 0x35, 0x70, 0x20, 0x01};
    IP4_ADDR(&ip, 192, 168, 77, 10);
    IP4_ADDR(&mask, 255, 255, 255, 0);
    IP4_ADDR(&gateway, 0, 0, 0, 0);
    lwip_init();
    network_diag[0] = 1U;
    if (xemac_add(&g_netif, &ip, &mask, &gateway, mac,
                  XPAR_XEMACPS_0_BASEADDR) == NULL) return 0;
    g_lwip_input = g_netif.input;
    g_netif.input = counting_input;
    network_diag[0] = 2U;
    netif_set_default(&g_netif);
    netif_set_up(&g_netif);
    if (!dhcp_server_start(&g_netif)) return 0;
    network_diag[0] = 3U;
    if (!tcp_control_start()) return 0;
    if (!audio_stream_init()) return 0;
    g_fast_at = g_slow_at = g_link_at = uptime_millis();
    g_ready = 1;
    network_diag[0] = 4U;
    return 1;
}

void network_poll(void)
{
    uint32_t now;
    mic_capture_poll();
    if (!g_ready) return;
    ++network_diag[1];
    now = uptime_millis();
    /* The Xilinx lwIP port starts its hot-plug monitor only under FreeRTOS.
     * In NO_SYS mode we must drive the same PHY/GEM speed and link update. */
    if ((uint32_t)(now - g_link_at) >= 1000U) {
        eth_link_detect(&g_netif);
        g_link_at = now;
    }
    network_diag[2] = netif_is_link_up(&g_netif) ? 1U : 0U;
    xemacif_input(&g_netif);
    if ((uint32_t)(now - g_fast_at) >= 250U) {
        tcp_fasttmr();
        g_fast_at = now;
    }
    if ((uint32_t)(now - g_slow_at) >= 500U) {
        tcp_slowtmr();
        g_slow_at = now;
    }
    tcp_control_poll();
    audio_stream_poll();
}
