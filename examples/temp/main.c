/**
 * @file main.c
 * @brief Enabling the second
 * @author STM32World <lth@stm32world.com>
 * @date 2026
 *
 * Copyright (c) 2026 STM32World <lth@stm32world.com>
 *
 * Enabling the second core on the Raspberry Pi Pico (RP2040) using the Pico SDK,
 * with a focus on cross-platform compatibility between ARM and RISC-V cores.
 * This example demonstrates:
 * - Creating a 1 ms tick using the SDK's repeating timer, similar to STM32's SysTick
 * - Redirecting printf to both uart0 and USB CDC for versatile debugging options
 * - Synchronizing access to printf across both cores using a mutex to prevent interleaved output
 *
 */

// Include necessary headers from the Pico SDK

#include "hardware/adc.h"    // For ADC access (if needed)
#include "hardware/clocks.h" // For clock frequency information
#include "hardware/dma.h"    //
#include "hardware/gpio.h"   // For GPIO control
#include "hardware/timer.h"  // Required for hardware timer access
#include "pico/multicore.h"  // For multicore support
#include "pico/stdlib.h"     // For sleep and stdio initialization

// Include standard I/O for printf
#include <stdint.h>
#include <stdio.h>

#ifndef LED_DELAY
#define LED_DELAY 500 // 500ms
#endif

#ifndef TICK_DELAY
#define TICK_DELAY 1000
#endif

// Mutex for synchronizing access to printf
auto_init_mutex(printf_mutex);

volatile uint16_t internal_temp_raw = 0;
volatile float internal_temp_c = 0.0f;

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

// This timer callback fires automatically in the background
bool repeating_timer_callback(struct repeating_timer *t) {
    // Explicitly select channel 8 for RP2354B/RP2350B variants to avoid floating pins
    adc_select_input(8);

    // 1. Instantly pull a fresh, clean hardware conversion
    internal_temp_raw = adc_read();

    // 2. Perform the formula math immediately in the background
    float voltage = internal_temp_raw * (3.3f / 4096.0f);
    internal_temp_c = 27.0f - (voltage - 0.706f) / 0.001721f;

    return true; // Keep the timer running endlessly
}

void init_automatic_temp_sensor() {
    // Initialize the ADC hardware block
    adc_init();
    adc_set_temp_sensor_enabled(true);

    // Hardcode channel 8 configuration to match the physical QFN-80 layout
    adc_select_input(8);

    // Create a background hardware timer that fires every 100 milliseconds
    static struct repeating_timer timer;
    add_repeating_timer_ms(-100, repeating_timer_callback, NULL, &timer);
}

/**
 * @brief Entry point for Core 1.
 */
void core1_entry() {

    mutex_enter_blocking(&printf_mutex); // Synchronize with Core 0 for printing
    printf("Core 1: Booting...\n");
    mutex_exit(&printf_mutex);

    uint32_t now, loop_cnt = 0, next_tick = TICK_DELAY + (TICK_DELAY / 2); // Start Core 1's ticks offset from Core 0

    while (1) {

        // FIX 3: Read volatile variable atomically into a local copy
        now = systick;

        if (now >= next_tick) {
            mutex_enter_blocking(&printf_mutex);
            printf("Core 1 tick %lu (loop = %lu)\n", now, loop_cnt);
            mutex_exit(&printf_mutex);
            loop_cnt = 0;
            next_tick = now + TICK_DELAY;
        }

        ++loop_cnt;

        // Give the memory bus and Core 0 a chance to breathe
        tight_loop_contents();
    }
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

    mutex_enter_blocking(&printf_mutex); // Mutex is not strictly necessary here since Core 1 hasn't started yet, but it's good practice to be consistent
    printf("\n\n\nCore 0: Booting...\n");
    printf("Running on %s at %d MHz\n",
#ifdef __riscv
           "RISC-V",
#else
           "Arm Cortex-M33",
#endif
           frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_SYS) / 1000);
    mutex_exit(&printf_mutex);

    // Start the heartbeat (Cross-Platform)
    universal_tick_init();

    // Initialize the automatic temperature sensor reading via ADC and DMA
    init_automatic_temp_sensor();

    // Launch core1_entry function on Core 1
    multicore_launch_core1(core1_entry);

    uint32_t now, loop_cnt = 0, next_blink = LED_DELAY, next_tick = TICK_DELAY;

    while (true) {

        // FIX 3: Read volatile variable atomically into a local copy
        now = systick;

        if (now >= next_blink) {
            pico_toggle_led();
            next_blink = now + LED_DELAY;
        }

        if (now >= next_tick) {

            // FIX 1: Accessing volatile multivariable sets atomically using local registers
            uint16_t local_raw = internal_temp_raw;
            float local_c = internal_temp_c;

            mutex_enter_blocking(&printf_mutex); // Ensure we've got exclusive access to printf
            printf("Core 0 tick %lu (loop = %lu raw = %u c = %.2f)\n", now, loop_cnt, (unsigned int)local_raw, local_c);
            mutex_exit(&printf_mutex);
            loop_cnt = 0;
            next_tick = now + TICK_DELAY;
        }

        // FIX 2: Prevent Core 0 from starving the bus/SIO registers during maximum performance loop runs
        tight_loop_contents();

        ++loop_cnt;
    }
}

// vim: ts=4 et nowrap