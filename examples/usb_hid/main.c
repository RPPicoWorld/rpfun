/**
 * @file main.c
 * @brief USB HID example for Raspberry Pi Pico using the Pico SDK.
 * @author STM32World <lth@stm32world.com>
 * @date 2026
 *
 * Copyright (c) 2026 STM32World <lth@stm32world.com>
 *
 * Enabling USB HID functionality on the Raspberry Pi Pico using the Pico SDK's USB stack, which is
 * compatible with both ARM and RISC-V cores. This example demonstrates:
 * - Creating a USB HID device
 * - Handling USB HID reports
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

#include "tusb.h"            // TinyUSB stack for USB functionality
#include "usb_descriptors.h" // USB descriptor definitions

#define LED_DELAY 500      // 500ms
#define BLINK_MOUNTED 1000 // 1s
#define TICK_DELAY 1000

// Volatile variable to mimic STM32's uwTick
static volatile uint32_t systick = 0;
volatile uint32_t blink_interval_ms = LED_DELAY;

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer, uint16_t bufsize);
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen);

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
    // board_init_after_tusb();

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

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer, uint16_t bufsize) {
    (void)instance;

    if (report_type == HID_REPORT_TYPE_OUTPUT) {
        // Set keyboard LED e.g Capslock, Numlock etc...
        if (report_id == REPORT_ID_KEYBOARD) {
            // bufsize should be (at least) 1
            if (bufsize < 1)
                return;

            uint8_t const kbd_leds = buffer[0];

            if (kbd_leds & KEYBOARD_LED_CAPSLOCK) {
                // Capslock On: disable blink, turn led on
                blink_interval_ms = 0;
                // board_led_write(true);
            } else {
                // Caplocks Off: back to normal blink
                // board_led_write(false);
                blink_interval_ms = BLINK_MOUNTED;
            }
        }
    }
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen) {
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)reqlen;

    // TODO not Implemented
    return 0;
}

// vim: ts=4 et nowrap