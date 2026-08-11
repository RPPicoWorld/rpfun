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

// Include standard I/O for printf
#include <stdint.h>
#include <stdio.h>

#include "dhcps.h"
#include "tusb.h"

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

    uint32_t now, loop_cnt = 0, next_blink = blink_interval_ms, next_tick = TICK_DELAY;

    while (true) {

        tud_task(); // tinyusb device task

        now = systick;

        if (now >= next_blink) {
            pico_toggle_led();
            next_blink = now + blink_interval_ms;
        }

        if (now >= next_tick) {
            printf("Core 0 tick %lu (loop = %lu)\n", now, loop_cnt);
            loop_cnt = 0;
            next_tick = now + TICK_DELAY;
        }

        ++loop_cnt;
    }
}

// vim: ts=4 et nowrap