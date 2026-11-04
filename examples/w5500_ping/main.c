/**
 * @file main.c
 * @brief Initialize W5500 and fire up DHCP
 * @author STM32World <lth@stm32world.com>
 * @date 2026
 *
 * Copyright (c) 2026 STM32World <lth@stm32world.com>
 *
 * Initializing the W5500 Ethernet controller and starting the DHCP client.
 */

// Include necessary headers from the Pico SDK
#include "hardware/clocks.h"  // For clock frequency information
#include "hardware/gpio.h"    // For GPIO control
#include "hardware/spi.h"     // For SPI communication
#include "hardware/timer.h"   // Required for hardware timer access
#include "pico/aon_timer.h"   // For RP2350/RP2354 Always-On Timer (pico_aon_timer library)
#include "pico/binary_info.h" // For binary information macros
#include "pico/stdlib.h"      // For sleep and stdio initialization

// Include standard I/O and time headers
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// W5500 and networking headers
#include "dhcp.h"
#include "dns.h"
#include "socket.h"
#include "w5500_spi.h"
#include "wizchip_conf.h"

#define LED_DELAY 500   // 500ms
#define TICK_DELAY 1000 // 1s

#define DHCP_SOCKET 0 // W5500 Socket 0 reserved for DHCP
#define NIST_SOCKET 1
#define DNS_SOCKET 2
#define NIST_PORT 13

// Volatile variable to mimic STM32's uwTick
static volatile uint32_t systick = 0;
volatile uint32_t blink_interval_ms = LED_DELAY;

static uint8_t dhcp_buffer[548]; // Working buffer required by ioLibrary DHCP
static uint8_t dhcp_allocated = 0;

// Default MAC and Network configuration structure
static wiz_NetInfo g_net_info = {
    .mac = { 0x00, 0x08, 0xDC, 0x12, 0x34, 0x56 },
    .ip = { 0, 0, 0, 0 },
    .sn = { 0, 0, 0, 0 },
    .gw = { 0, 0, 0, 0 },
    .dns = { 0, 0, 0, 0 },
    .dhcp = NETINFO_DHCP
};

// NIST Time Server: time.nist.gov (132.163.96.1)
static uint8_t nist_server_ip[4] = { 0, 0, 0, 0 }; // Resolved dynamically

// Repeating timer struct for DHCP tick
static struct repeating_timer dhcp_timer;

// Flag indicating if AON timer has been synchronized with NIST
static bool aon_timer_synced = false;

// -----------------------------------------------------------------------------
// Prototypes
// -----------------------------------------------------------------------------
void print_network_info(void);
void callback_ip_assigned(void);
void callback_ip_conflict(void);
bool dhcp_timer_callback(struct repeating_timer *t);
void fetch_nist_daytime(void);
bool parse_nist_daytime(const char *str, struct tm *tm_out);

/**
 * @brief Callback for the repeating timer.
 * @details Works on both ARM and RISC-V.
 */
bool on_timer_tick(struct repeating_timer *t) {
    (void)t;
    systick++;
    return true; // Keep the timer running
}

/**
 * @brief Universal tick initialization using the SDK timer pool.
 */
void universal_tick_init(void) {
    static struct repeating_timer timer;
    // Negative delay means "measure from the start of the last callback"
    // to avoid jitter. -1ms = 1000us frequency.
    add_repeating_timer_ms(-1, on_timer_tick, NULL, &timer);
}

// Perform initialisation
int pico_led_init(void) {
    gpio_init(PICO_DEFAULT_LED_PIN);              // The LED pin is defined in the board header as PICO_DEFAULT_LED_PIN
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT); // Set the LED pin as an output
    return PICO_OK;
}

/**
 * @brief Toggles the state of the default LED.
 */
void pico_toggle_led(void) {
    gpio_xor_mask64(((uint64_t)1 << PICO_DEFAULT_LED_PIN));
}

// -----------------------------------------------------------------------------
// Helper & Callbacks Implementation
// -----------------------------------------------------------------------------
void print_network_info(void) {
    wiz_NetInfo netInfo;
    wizchip_getnetinfo(&netInfo);
    printf("\n--- Network Configured via DHCP ---\n");
    printf("IP : %d.%d.%d.%d\n", netInfo.ip[0], netInfo.ip[1], netInfo.ip[2], netInfo.ip[3]);
    printf("SN : %d.%d.%d.%d\n", netInfo.sn[0], netInfo.sn[1], netInfo.sn[2], netInfo.sn[3]);
    printf("GW : %d.%d.%d.%d\n", netInfo.gw[0], netInfo.gw[1], netInfo.gw[2], netInfo.gw[3]);
    printf("DNS: %d.%d.%d.%d\n", netInfo.dns[0], netInfo.dns[1], netInfo.dns[2], netInfo.dns[3]);
    printf("----------------------------------\n\n");
}

/**
 * @brief Callback invoked when DHCP successfully retrieves an IP address
 */
void callback_ip_assigned(void) {
    // Retrieve acquired network parameters from DHCP engine
    getIPfromDHCP(g_net_info.ip);
    getGWfromDHCP(g_net_info.gw);
    getSNfromDHCP(g_net_info.sn);
    getDNSfromDHCP(g_net_info.dns);
    g_net_info.dhcp = NETINFO_DHCP;

    // Apply acquired network parameters to the W5500 hardware
    wizchip_setnetinfo(&g_net_info);

    dhcp_allocated = 1;
    printf("[DHCP SUCCESS] IP Address Lease Acquired!\n");
    print_network_info();
}

/**
 * @brief Callback invoked if IP conflict is detected on network
 */
void callback_ip_conflict(void) {
    dhcp_allocated = 0;
    printf("[DHCP ERROR] IP Conflict Detected!\n");
}

/**
 * @brief Pico SDK Repeating Timer Callback - Handles 1-second ticks for DHCP engine
 */
bool dhcp_timer_callback(struct repeating_timer *t) {
    (void)t;
    DHCP_time_handler(); // Must be called once every 1 second
    return true;         // Keep timer repeating
}

/**
 * @brief Resolves hostname to IP using the W5500 DNS client.
 * @return true on successful resolution, false on failure.
 */
bool resolve_hostname(const char *domain, uint8_t *out_ip) {

    uint8_t dns_buffer[2048];

    // Ensure we have a valid DNS server from DHCP
    if (g_net_info.dns[0] == 0) {
        printf("[DNS] Error: No DNS server set!\n");
        return false;
    }

    // Initialize WIZnet DNS engine
    DNS_init(DNS_SOCKET, dns_buffer);

    printf("[DNS] Resolving %s via %d.%d.%d.%d...\n", domain, g_net_info.dns[0], g_net_info.dns[1], g_net_info.dns[2], g_net_info.dns[3]);

    // Execute DNS query (3 retry attempts)
    int8_t res = DNS_run(g_net_info.dns, (uint8_t *)domain, out_ip);

    if (res == 1) {
        printf("[DNS] Resolved %s -> %d.%d.%d.%d\n", domain, out_ip[0], out_ip[1], out_ip[2], out_ip[3]);
        return true;
    } else {
        printf("[DNS] Resolution failed for %s (Error code: %d)\n", domain, res);
        return false;
    }
}

/**
 * @brief Parses NIST daytime format: "JJJJJ YY-MM-DD HH:MM:SS TT L H ADV UTC(NIST) *"
 */
bool parse_nist_daytime(const char *str, struct tm *tm_out) {
    int jj;
    int yy, mm, dd, hh, min, ss;

    if (sscanf(str, "%d %2d-%2d-%2d %2d:%2d:%2d", &jj, &yy, &mm, &dd, &hh, &min, &ss) == 7) {
        memset(tm_out, 0, sizeof(struct tm));
        tm_out->tm_year = (2000 + yy) - 1900; // Years since 1900
        tm_out->tm_mon = mm - 1;              // Month 0-11
        tm_out->tm_mday = dd;                 // Day of month 1-31
        tm_out->tm_hour = hh;
        tm_out->tm_min = min;
        tm_out->tm_sec = ss;
        return true;
    }

    return false;
}

/**
 * @brief Connect to NIST server and output time payload using systick
 */
void fetch_nist_daytime(void) {
    uint8_t rx_buf[128];
    uint32_t timeout;

    if (!resolve_hostname("time.nist.gov", nist_server_ip)) {
        printf("[TIME] Aborting fetch: DNS lookup failed.\n");
        return;
    }

    // Ensure socket is closed before starting
    close(NIST_SOCKET);

    // Open TCP socket
    if (socket(NIST_SOCKET, Sn_MR_TCP, 8000, 0) != NIST_SOCKET) {
        printf("[TIME] Failed to open socket\n");
        return;
    }

    // Connect to NIST server using resolved IP
    connect(NIST_SOCKET, nist_server_ip, NIST_PORT);

    // Wait for connection or early data arrival (timeout after 2 seconds using systick)
    timeout = systick + 2000;
    while (getSn_SR(NIST_SOCKET) != SOCK_ESTABLISHED && getSn_SR(NIST_SOCKET) != SOCK_CLOSE_WAIT) {
        if (getSn_RX_RSR(NIST_SOCKET) > 0)
            break; // NIST sent payload early
        if (systick >= timeout) {
            printf("[TIME] Connection timeout (SR=0x%02X)\n", getSn_SR(NIST_SOCKET));
            close(NIST_SOCKET);
            return;
        }
    }

    // Wait for NIST server to send daytime payload (timeout after 2 seconds using systick)
    timeout = systick + 2000;
    while (getSn_RX_RSR(NIST_SOCKET) == 0) {
        uint8_t sr = getSn_SR(NIST_SOCKET);
        if (sr == SOCK_CLOSED || sr == SOCK_CLOSE_WAIT)
            break;
        if (systick >= timeout) {
            printf("[TIME] Receive timeout (SR=0x%02X, RX_RSR=%d)\n", getSn_SR(NIST_SOCKET), getSn_RX_RSR(NIST_SOCKET));
            close(NIST_SOCKET);
            return;
        }
    }

    // Read received time string
    uint16_t len = getSn_RX_RSR(NIST_SOCKET);
    if (len > 0) {
        if (len > sizeof(rx_buf) - 1) {
            len = sizeof(rx_buf) - 1;
        }

        recv(NIST_SOCKET, rx_buf, len);
        rx_buf[len] = '\0';

        // Skip leading newlines/spaces if present
        uint8_t *p = rx_buf;
        while (*p == '\r' || *p == '\n' || *p == ' ') {
            p++;
        }

        printf("[NIST TIME] %s", p);

        struct tm parsed_tm;
        if (parse_nist_daytime((const char *)p, &parsed_tm)) {
            aon_timer_start_calendar(&parsed_tm);
            aon_timer_synced = true;
            printf("[RTC/AON] Synced to NIST: %04d-%02d-%02d %02d:%02d:%02d UTC\n", parsed_tm.tm_year + 1900, parsed_tm.tm_mon + 1, parsed_tm.tm_mday, parsed_tm.tm_hour, parsed_tm.tm_min, parsed_tm.tm_sec);
        } else {
            printf("[RTC/AON] Error: Failed to parse NIST time string\n");
        }
    } else {
        printf("[TIME] No data received (SR=0x%02X)\n", getSn_SR(NIST_SOCKET));
    }

    // Clean disconnect and close
    disconnect(NIST_SOCKET);
    close(NIST_SOCKET);
}

/**
 * @brief Main entry point for Core 0.
 */
int main(void) {

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

    printf("\n\n\nStart RP2354 W5500 DHCP Client\n");

    // Configure status LED
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);

    // Initialize W5500 hardware and registered SPI callbacks
    if (W5500_Init(&g_net_info) != 0) {
        printf("[ERROR] W5500 Init Failed!\n");
        while (1) {
            tight_loop_contents();
        }
    }

    printf("[W5500] Init OK. Version Register: 0x%02X\n", getVERSIONR());

    // Initialize DHCP Client module
    DHCP_init(DHCP_SOCKET, dhcp_buffer);

    // Register DHCP Callbacks
    reg_dhcp_cbfunc(callback_ip_assigned, callback_ip_assigned, callback_ip_conflict);

    // Start 1-second repeating hardware timer for DHCP ticks
    add_repeating_timer_ms(1000, dhcp_timer_callback, NULL, &dhcp_timer);
    printf("[SYS] Repeating timer started.\n");

    printf("[DHCP] Starting DHCP State Machine...\n");

    uint32_t now, loop_cnt = 0, next_blink = blink_interval_ms, next_tick = TICK_DELAY, next_time_poll = 10000;
    uint8_t dhcp_state = 0, last_dhcp_state = 0xFF;
    struct tm current_tm;

    while (true) {

        now = systick;

        /* Run DHCP state machine: every loop until IP is acquired, then once per second for lease maintenance */
        if (!dhcp_allocated || (now >= next_tick)) {
            dhcp_state = DHCP_run();

            if (dhcp_state != last_dhcp_state) {
                switch (dhcp_state) {
                case DHCP_RUNNING:
                    printf("[DHCP] Discovering/Requesting IP...\n");
                    break;
                case DHCP_IP_ASSIGN:
                case DHCP_IP_CHANGED:
                case DHCP_IP_LEASED:
                    break;
                case DHCP_FAILED:
                    printf("[DHCP] Request Failed!\n");
                    break;
                }
                last_dhcp_state = dhcp_state;
            }
        }

        if (now >= next_blink) {
            pico_toggle_led();
            next_blink = now + blink_interval_ms;
        }

        if (now >= next_tick) {
            if (aon_timer_synced && aon_timer_get_time_calendar(&current_tm)) {
                printf("[CORE0] tick %lu (loop = %lu, dhcp_allocated = %d) | %04d-%02d-%02d %02d:%02d:%02d\n", now, loop_cnt, dhcp_allocated, current_tm.tm_year + 1900, current_tm.tm_mon + 1, current_tm.tm_mday, current_tm.tm_hour, current_tm.tm_min, current_tm.tm_sec);
            } else {
                printf("[CORE0] tick %lu (loop = %lu, dhcp_allocated = %d) | RTC Not Set\n", now, loop_cnt, dhcp_allocated);
            }
            loop_cnt = 0;
            next_tick = now + TICK_DELAY;
        }

        if (dhcp_allocated && (now >= next_time_poll)) {
            printf("[TIME] Polling NIST Daytime server...\n");
            fetch_nist_daytime();
            next_time_poll = systick + 60000; // Poll once per minute (60,000 ms)
        }

        ++loop_cnt;
    }
}

// vim: ts=4 et nowrap