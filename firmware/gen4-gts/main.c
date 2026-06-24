/*
 * pico-ball Gen 4 GTS server — Pico 2 W standalone.
 *
 * Brings up an open SoftAP, runs DHCP + a catch-all DNS so a Nintendo DS
 * points all of nintendowifi.net at this board, then serves the WFC front
 * door (connection test + NAS login) and the Gen 4 GTS endpoints. Deposit a
 * Pokemon into the in-game GTS and it is captured here; the capture is dumped
 * as hex over USB serial.
 *
 * No link-cable hardware is involved — this is pure WiFi. A bare Pico 2 W on
 * USB power is sufficient.
 */
#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/ip_addr.h"
#include "lwip/netif.h"

#include "dhcpserver.h"
#include "dnsserver.h"
#include "wfc_http.h"
#include "gts.h"

#define AP_SSID "pico-ball-gts"     /* open network, no password */

static dhcp_server_t dhcp;
static dns_server_t  dns;

int main(void) {
    stdio_init_all();
    sleep_ms(2500);                 /* let USB CDC enumerate for logs */
    printf("\n=== pico-ball Gen 4 GTS server ===\n");

    if (cyw43_arch_init()) {
        printf("cyw43_arch_init failed\n");
        return 1;
    }

    cyw43_arch_enable_ap_mode(AP_SSID, NULL, CYW43_AUTH_OPEN);
    printf("SoftAP up: SSID '%s' (open)\n", AP_SSID);

    ip_addr_t gw, mask;
    IP4_ADDR(ip_2_ip4(&gw),   192, 168, 4, 1);
    IP4_ADDR(ip_2_ip4(&mask), 255, 255, 255, 0);

    struct netif *ap = &cyw43_state.netif[CYW43_ITF_AP];
    netif_set_addr(ap, ip_2_ip4(&gw), ip_2_ip4(&mask), ip_2_ip4(&gw));

    dhcp_server_init(&dhcp, &gw, &mask);
    dns_server_init(&dns, &gw);     /* every lookup -> 192.168.4.1 */
    gts_init();
    if (wfc_http_init() != 0) {
        printf("http init failed\n");
        return 1;
    }

    printf("DS WFC manual config: AP '%s', DHCP on, "
           "or set Primary DNS = 192.168.4.1\n", AP_SSID);
    printf("Waiting for the DS...\n");

    int last = -1;
    while (true) {
#if PICO_CYW43_ARCH_POLL
        cyw43_arch_poll();
        cyw43_arch_wait_for_work_until(make_timeout_time_ms(1000));
#else
        sleep_ms(500);
#endif
        if (gts_capture_count != last) {
            last = gts_capture_count;
            printf("[status] captures this session: %d\n", last);
        }
    }
}
