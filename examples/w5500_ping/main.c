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
#include "pico/binary_info.h" // For binary information macros
#include "pico/stdlib.h"      // For sleep and stdio initialization

// Include standard I/O for printf
#include <stdint.h>
#include <stdio.h>

// W5500 and networking headers
#include "dhcp.h"
#include "socket.h"
#include "w5500_spi.h"
#include "wizchip_conf.h"

#define LED_DELAY 500   // 500ms
#define TICK_DELAY 1000 // 1s

#define DHCP_SOCKET 0 // W5500 Socket 0 reserved for DHCP
#define nist_socket 1
#define nist_port 13

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
static uint8_t nist_server_ip[4] = { 132, 163, 96, 1 };

// Repeating timer struct for DHCP tick
static struct repeating_timer dhcp_timer;

// -----------------------------------------------------------------------------
// Prototypes
// -----------------------------------------------------------------------------
void Print_Network_Info(void);
void Callback_IPAssigned(void);
void Callback_IPConflict(void);
bool dhcp_timer_callback(struct repeating_timer *t);
void fetch_nist_daytime(void);

/**
 * @brief Callback for the repeating timer.
 * Works on both ARM and RISC-V.
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
void Print_Network_Info(void) {
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
void Callback_IPAssigned(void) {
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
    Print_Network_Info();
}

/**
 * @brief Callback invoked if IP conflict is detected on network
 */
void Callback_IPConflict(void) {
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
 * @brief Connect to NIST server and output time payload using systick
 */
void fetch_nist_daytime(void) {
    uint8_t rx_buf[128];
    uint32_t timeout;

    // 1. Ensure socket is closed before starting
    close(nist_socket);

    // 2. Open TCP socket
    if (socket(nist_socket, Sn_MR_TCP, 8000, 0) != nist_socket) {
        printf("[TIME] Failed to open socket\n");
        return;
    }

    // 3. Connect to NIST server
    connect(nist_socket, nist_server_ip, nist_port);

    // 4. Wait for connection or early data arrival (timeout after 2 seconds using systick)
    timeout = systick + 2000;
    while (getSn_SR(nist_socket) != SOCK_ESTABLISHED && getSn_SR(nist_socket) != SOCK_CLOSE_WAIT) {
        if (getSn_RX_RSR(nist_socket) > 0)
            break; // NIST sent payload early
        if (systick >= timeout) {
            printf("[TIME] Connection timeout (SR=0x%02X)\n", getSn_SR(nist_socket));
            close(nist_socket);
            return;
        }
    }

    // 5. Wait for NIST server to send daytime payload (timeout after 2 seconds using systick)
    timeout = systick + 2000;
    while (getSn_RX_RSR(nist_socket) == 0) {
        uint8_t sr = getSn_SR(nist_socket);
        if (sr == SOCK_CLOSED || sr == SOCK_CLOSE_WAIT)
            break;
        if (systick >= timeout) {
            printf("[TIME] Receive timeout (SR=0x%02X, RX_RSR=%d)\n", getSn_SR(nist_socket), getSn_RX_RSR(nist_socket));
            close(nist_socket);
            return;
        }
    }

    // 6. Read received time string
    uint16_t len = getSn_RX_RSR(nist_socket);
    if (len > 0) {
        if (len > sizeof(rx_buf) - 1) {
            len = sizeof(rx_buf) - 1;
        }

        recv(nist_socket, rx_buf, len);
        rx_buf[len] = '\0';

        // Skip leading newlines/spaces if present
        uint8_t *p = rx_buf;
        while (*p == '\r' || *p == '\n' || *p == ' ') {
            p++;
        }

        printf("[NIST TIME] %s", p);
    } else {
        printf("[TIME] No data received (SR=0x%02X)\n", getSn_SR(nist_socket));
    }

    // 7. Clean disconnect and close
    disconnect(nist_socket);
    close(nist_socket);
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
    reg_dhcp_cbfunc(Callback_IPAssigned, Callback_IPAssigned, Callback_IPConflict);

    // Start 1-second repeating hardware timer for DHCP ticks
    add_repeating_timer_ms(1000, dhcp_timer_callback, NULL, &dhcp_timer);
    printf("[SYS] Repeating timer started.\n");

    printf("[DHCP] Starting DHCP State Machine...\n");

    uint32_t now, loop_cnt = 0, next_blink = blink_interval_ms, next_tick = TICK_DELAY, next_time_poll = 10000;
    uint8_t dhcp_state = 0, last_dhcp_state = 0xFF;

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
            printf("Core 0 tick %lu (loop = %lu, dhcp_allocated = %d)\n", now, loop_cnt, dhcp_allocated);
            loop_cnt = 0;
            next_tick = now + TICK_DELAY;
        }

        if (dhcp_allocated && (now >= next_time_poll)) {
            printf("[TIME] Polling NIST Daytime server...\n");
            fetch_nist_daytime();
            next_time_poll = systick + 10000; // Reset timer relative to current systick AFTER execution
        }

        ++loop_cnt;
    }
}

// vim: ts=4 et nowrap