/**
 * @file main.c
 * @brief Using the can2040 library to demonstrate CAN bus functionality on the RP2040, along with a simple LED blink and tick example.
 * @author STM32World <lth@stm32world.com>
 * @date 2026
 *
 * Copyright (c) 2026 STM32World <lth@stm32world.com>
 *
 * Receive and send CAN messages using the can2040 library on the RP2040. This example demonstrates how to set
 * up the CAN bus, handle incoming messages, and transmit messages. It also includes a simple LED blink and
 * tick example to show that the program is running.
 *
 */

// Include necessary headers from the Pico SDK
#include "hardware/clocks.h" // For clock frequency information
#include "hardware/dma.h"    // For DMA access (if needed)
#include "hardware/gpio.h"   // For GPIO control
#include "hardware/timer.h"  // Required for hardware timer access
#include "hardware/vreg.h"   // Needed for voltage scaling
#include "pico/multicore.h"  // For multicore support
#include "pico/rand.h"       // For random number generation
#include "pico/stdlib.h"     // For sleep and stdio initialization

// Include standard I/O for printf
#include <stdint.h>
#include <stdio.h>

#define LED_DELAY 500   // LED blink delay in milliseconds
#define TICK_DELAY 1000 // Tick delay in milliseconds

// Volatile variable to mimic STM32's uwTick
static volatile uint32_t systick = 0;

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

/**
 * @brief Initializes the default LED GPIO pin.
 * @return PICO_OK on success, error code on failure.
 */
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

    uint32_t now, loop_cnt = 0, next_blink = LED_DELAY, next_tick = TICK_DELAY;

    // Main loop for Core 0
    while (true) {

        now = systick;

        if (now >= next_blink) {
            pico_toggle_led();
            next_blink = now + LED_DELAY;
        }

        if (now >= next_tick) {
            printf("RP tick %lu (loop = %lu)\n", now / 1000, loop_cnt);
            loop_cnt = 0;
            next_tick = now + TICK_DELAY;
        }

        ++loop_cnt;

        tight_loop_contents(); // Allow other tasks to run and prevent CPU hogging
    }
}

// vim: ts=4 et nowrap
