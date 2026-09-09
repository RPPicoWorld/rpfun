/**
 * @file main.c
 * @brief USB Ethernet example for Raspberry Pi Pico using the Pico SDK and TinyUSB stack.
 * @author STM32World <lth@stm32world.com>
 * @date 2026
 *
 * Copyright (c) 2026 STM32World <lth@stm32world.com>
 *
 * Enabling USB Ethernet functionality on the Raspberry Pi Pico using the Pico SDK's USB stack, which is
 * compatible with both ARM and RISC-V cores. This example demonstrates:
 * - Creating a USB Ethernet device
 * - Handling USB Ethernet packets
 *
 */

// Include necessary headers from the Pico SDK

#include "hardware/clocks.h" // For clock frequency information
#include "hardware/gpio.h"   // For GPIO control
#include "hardware/timer.h"  // Required for hardware timer access
#include "pico/stdlib.h"     // For sleep and stdio initialization
#include "pico/unique_id.h"

// Include standard I/O for printf
#include <stdint.h>
#include <stdio.h>

#include "dhcps.h"
#include "tusb.h"

#include "lwip/apps/fs.h"
#include "lwip/apps/httpd.h"
#include "lwip/dhcp.h"
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/timeouts.h"
#include "netif/etharp.h"

#define LED_DELAY 500      // 500ms
#define BLINK_MOUNTED 1000 // 1s
#define TICK_DELAY 1000

// Volatile variable to mimic STM32's uwTick
static volatile uint32_t systick = 0;
volatile uint32_t blink_interval_ms = LED_DELAY;

static struct netif netif_data;

// Buffer to store generated HTML response
static char http_response_buf[2048];

/**
 * @brief Callback for the repeating timer.
 * Works on both ARM and RISC-V.
 */
bool on_timer_tick(struct repeating_timer *t) {
    systick++;
    return true; // Keep the timer running
}

/**
 * @brief Universal tick initialization using the SDK timer pool.
 */
void universal_tick_init() {
    static struct repeating_timer timer;
    // Negative delay means "measure from the start of the last callback"
    // to avoid jitter. -1ms = 1000us frequency.
    add_repeating_timer_ms(-1, on_timer_tick, NULL, &timer);
}

// Perform initialisation
int pico_led_init(void) {
    gpio_init(PICO_DEFAULT_LED_PIN);              // The LED pin is defined in the board header as PICO_DEFAULT_LED_PIN
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT); // Set the LED pin as an output
    gpio_put(PICO_DEFAULT_LED_PIN, 1);            // Drive high immediately
    return PICO_OK;
}

/**
 * @brief Toggles the state of the default LED.
 */
void pico_toggle_led() {
    gpio_xor_mask64(((uint64_t)1 << PICO_DEFAULT_LED_PIN));
}

/* TinyUSB MAC address definition */
uint8_t tud_network_mac_address[6] = { 0x02, 0x00, 0x00, 0x12, 0x34, 0x56 };

/* Callback: Output frame from lwIP down to TinyUSB */
static err_t linkoutput_fn(struct netif *netif, struct pbuf *p) {
    (void)netif;

    // Check if TinyUSB can accept a network packet
    if (!tud_network_can_xmit(p->tot_len)) {
        return ERR_MEM;
    }

    // Allocate buffer and copy payload
    tud_network_xmit(p, 0); // TinyUSB provides zero-copy or callback handling
    return ERR_OK;
}

/* Callback: Called by TinyUSB when lwIP sends packet */
uint16_t tud_network_xmit_cb(uint8_t *dst, void *ref, uint16_t arg) {
    struct pbuf *p = (struct pbuf *)ref;
    return pbuf_copy_partial(p, dst, p->tot_len, 0);
}

/* Init low-level netif hardware interface */
static err_t ip_init_cb(struct netif *netif) {
    netif->linkoutput = linkoutput_fn;
    netif->output = etharp_output;
    netif->mtu = 1500;
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_LINK_UP;

    memcpy(netif->hwaddr, tud_network_mac_address, 6);
    netif->hwaddr_len = 6;
    return ERR_OK;
}

/* Callback: TinyUSB received a packet from Host */
bool tud_network_recv_cb(const uint8_t *src, uint16_t size) {
    if (size == 0)
        return true;

    // Allocate an lwIP pbuf for incoming packet
    struct pbuf *p = pbuf_alloc(PBUF_RAW, size, PBUF_POOL);
    if (p) {
        pbuf_take(p, src, size);
        // Pass packet into lwIP stack
        if (netif_data.input(p, &netif_data) != ERR_OK) {
            pbuf_free(p);
        }
    }
    tud_network_recv_renew(); // Signal TinyUSB to receive next packet
    return true;
}

void tud_network_init_cb(void) {
    // Called when USB Network interface is initialized
}

/* Custom fs_open_custom handler for HTML shell and JSON APIs */
int fs_open_custom(struct fs_file *file, const char *name) {

    // --- LED TOGGLE ENDPOINT: /api/led/toggle ---
    if (strcmp(name, "/api/led/toggle") == 0) {
        pico_toggle_led(); // Perform action

        int len = snprintf(http_response_buf, sizeof(http_response_buf), "{\"status\":\"ok\",\"led\":%d}", gpio_get(PICO_DEFAULT_LED_PIN));

        memset(file, 0, sizeof(struct fs_file));
        file->data = http_response_buf;
        file->len = len;
        file->index = len;
        file->flags = FS_FILE_FLAGS_CUSTOM;
        return 1;
    }

    // --- JSON API ENDPOINT: /api/stats ---
    if (strcmp(name, "/api/stats") == 0) {
        pico_unique_board_id_t id;
        pico_get_unique_board_id(&id);

        char id_str[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];
        pico_get_unique_board_id_string(id_str, sizeof(id_str));

        uint32_t sys_clk_mhz = frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_SYS) / 1000;
        uint32_t uptime_sec = systick / 1000;

        int len = snprintf(http_response_buf, sizeof(http_response_buf), "{"
                                                                         "\"architecture\":\"%s\","
                                                                         "\"clock_mhz\":%lu,"
                                                                         "\"board_id\":\"%s\","
                                                                         "\"uptime_seconds\":%lu"
                                                                         "}",
#ifdef __riscv
                           "RISC-V (Hazard3)",
#else
                           "Arm Cortex-M33",
#endif
                           (unsigned long)sys_clk_mhz,
                           id_str,
                           (unsigned long)uptime_sec);

        memset(file, 0, sizeof(struct fs_file));
        file->data = http_response_buf;
        file->len = len;
        file->index = len;
        file->flags = FS_FILE_FLAGS_CUSTOM;
        return 1;
    }

    // --- HTML WEB PAGE: Dynamic Shell with LED Control Button ---
    if (strcmp(name, "/index.html") == 0 || strcmp(name, "/") == 0) {
        int len = snprintf(http_response_buf, sizeof(http_response_buf), "<!DOCTYPE html><html><head><title>RP2350 Info</title>"
                                                                         "<style>"
                                                                         "body{font-family:sans-serif;margin:40px;background:#1a1a1a;color:#eee}"
                                                                         "h1{color:#e6005c}.card{background:#2a2a2a;padding:20px;border-radius:8px;"
                                                                         "max-width:500px;box-shadow:0 4px 10px rgba(0,0,0,0.5)}"
                                                                         "code{background:#333;padding:2px 6px;border-radius:4px;color:#00ffcc}"
                                                                         "button{background:#e6005c;color:#fff;border:none;padding:10px 18px;"
                                                                         "font-size:14px;border-radius:4px;cursor:pointer;margin-top:10px;font-weight:bold}"
                                                                         "button:hover{background:#ff1a75}"
                                                                         "</style>"
                                                                         "<script>"
                                                                         "async function updateStats(){"
                                                                         "try{"
                                                                         "let r = await fetch('/api/stats');"
                                                                         "let d = await r.json();"
                                                                         "document.getElementById('arch').innerText = d.architecture;"
                                                                         "document.getElementById('clk').innerText = d.clock_mhz + ' MHz';"
                                                                         "document.getElementById('id').innerText = d.board_id;"
                                                                         "document.getElementById('up').innerText = d.uptime_seconds + ' s';"
                                                                         "}catch(e){console.error(e);}"
                                                                         "}"
                                                                         "async function toggleLed(){"
                                                                         "try{"
                                                                         "await fetch('/api/led/toggle');"
                                                                         "}catch(e){console.error(e);}"
                                                                         "}"
                                                                         "window.onload = () => {"
                                                                         "updateStats();"
                                                                         "setInterval(updateStats, 1000);"
                                                                         "};"
                                                                         "</script></head><body>"
                                                                         "<div class='card'><h1>RP2350 System Info</h1>"
                                                                         "<p><b>Architecture:</b> <span id='arch'>Loading...</span></p>"
                                                                         "<p><b>System Clock:</b> <span id='clk'>Loading...</span></p>"
                                                                         "<p><b>Unique Board ID:</b> <code id='id'>Loading...</code></p>"
                                                                         "<p><b>System Uptime:</b> <span id='up'>Loading...</span></p>"
                                                                         "<button onclick='toggleLed()'>Toggle LED</button>"
                                                                         "</div></body></html>");

        memset(file, 0, sizeof(struct fs_file));
        file->data = http_response_buf;
        file->len = len;
        file->index = len;
        file->flags = FS_FILE_FLAGS_CUSTOM;
        return 1;
    }

    return 0; // Fallback for unhandled paths
}

void fs_close_custom(struct fs_file *file) {
    (void)file;
}

/**
 * @brief Main entry point for Core 0.
 */
int main() {

    int rc = pico_led_init(); // Initialize the LED GPIO

    hard_assert(rc == PICO_OK); // Ensure LED initialization was successful

    stdio_init_all(); // Initialize all standard I/O (UART, USB, etc.)

    // Explicitly override the baud rate for UART0 to 921600 for better performance with the SDK's printf implementation
    uart_set_baudrate(uart0, 921600);

    // Give UART a moment to stabilize
    sleep_ms(50);

    printf("\n\n\nCore 0: Booting...\n");
    printf("Running on %s at %d MHz\n",
#ifdef __riscv
           "RISC-V",
#else
           "Arm Cortex-M33",
#endif
           frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_SYS) / 1000);

    // Start the heartbeat (Cross-Platform)
    universal_tick_init();

    // init device stack on configured roothub port
    const tusb_rhport_init_t rh_init = {
        .role = TUSB_ROLE_DEVICE,
        .speed = TUD_OPT_HIGH_SPEED ? TUSB_SPEED_HIGH : TUSB_SPEED_FULL
    };
    TU_ASSERT(tud_rhport_init(BOARD_TUD_RHPORT, &rh_init));

    lwip_init();

    // Setup network interface IP for RP2350 (192.168.7.1)
    ip4_addr_t ipaddr, netmask, gw;
    IP4_ADDR(&ipaddr, 192, 168, 10, 1);
    IP4_ADDR(&netmask, 255, 255, 255, 0);
    IP4_ADDR(&gw, 192, 168, 10, 1);

    netif_add(&netif_data, &ipaddr, &netmask, &gw, NULL, ip_init_cb, netif_input);
    netif_set_default(&netif_data);
    netif_set_up(&netif_data);

    // Initialize DHCP Server!
    dhcps_init(&ipaddr, &netmask);

    // Initialize HTTP Server
    httpd_init();

    uint32_t now, loop_cnt = 0, next_blink = blink_interval_ms, next_tick = TICK_DELAY;

    while (true) {

        tud_task(); // tinyusb device task

        // Service lwIP internal timers (CRITICAL for TCP connections)
        sys_check_timeouts();

        now = systick;

        // if (now >= next_blink) {
        //     pico_toggle_led();
        //     next_blink = now + blink_interval_ms;
        // }

        if (now >= next_tick) {
            printf("Core 0 tick %lu (loop = %lu)\n", now, loop_cnt);
            loop_cnt = 0;
            next_tick = now + TICK_DELAY;
        }

        ++loop_cnt;
    }
}

// vim: ts=4 et nowrap