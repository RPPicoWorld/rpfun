/**
 * @file main.c
 * @brief Overclocking RP2350
 * @author STM32World <lth@stm32world.com>
 * @date 2026
 *
 * Copyright (c) 2026 STM32World <lth@stm32world.com>
 *
 * Trying to push the RP2350 to its limits with a simple LED blink and tick example.
 *
 */

// Include necessary headers from the Pico SDK

#include "hardware/adc.h"    // For ADC access (if needed)
#include "hardware/clocks.h" // For clock frequency information
#include "hardware/dma.h"    // For DMA access (if needed)
// #include "hardware/flash.h"  // For flash memory access
#include "hardware/gpio.h"  // For GPIO control
#include "hardware/timer.h" // Required for hardware timer access
#include "hardware/vreg.h"  // Needed for voltage scaling
#include "pico/bootrom.h"   // For flash command execution
#include "pico/multicore.h" // For multicore support
#include "pico/stdlib.h"    // For sleep and stdio initialization

// Include standard I/O for printf
#include <stdint.h>
#include <stdio.h>

#ifndef LED_DELAY
#define LED_DELAY 500 // 500ms
#endif

#ifndef TICK_DELAY
#define TICK_DELAY 1000 // 1000ms = 1 second
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

    // 1. Instantly pull a fresh, clean hardware conversion (integer only)
    internal_temp_raw = adc_read();

    return true; // Keep the timer running endlessly
}

void init_automatic_temp_sensor() {
    // Initialize the ADC hardware block
    adc_init();
    adc_set_temp_sensor_enabled(true);

    // Hardcode channel 8 configuration to match the physical QFN-80 layout
    adc_select_input(8);

    // Create a background hardware timer that fires every 500 milliseconds
    static struct repeating_timer timer;
    add_repeating_timer_ms(-500, repeating_timer_callback, NULL, &timer);
}

/**
 * @brief Entry point for Core 1.
 */
void core1_entry() {

    mutex_enter_blocking(&printf_mutex); // Synchronize with Core 0 for printing
    printf("Core 1: Booting...\n");
    mutex_exit(&printf_mutex);

    uint32_t now, loop_cnt = 0, next_tick = TICK_DELAY + (TICK_DELAY / 2); // Start Core 1's ticks offset from Core 0

    // Main loop for Core 1
    while (1) {

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

    vreg_disable_voltage_limit(); // Disable voltage limit to allow higher voltages for overclocking

    vreg_set_voltage(VREG_VOLTAGE_1_60); // Set voltage to 1.60V for stable overclocking

    rom_flash_enter_cmd_xip(); // Enter XIP mode for flash access

    set_sys_clock_khz(540000, true); // Set system clock to 540 MHz, true means to wait for the clock to stabilize

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

    // Main loop for Core 0
    while (true) {

        now = systick;

        if (now >= next_blink) {
            pico_toggle_led();
            next_blink = now + LED_DELAY;
        }

        if (now >= next_tick) {

            // FIX: Load values into local variables constructed at runtime to prevent
            // the compiler from compiling literal float constants inside QSPI flash memory.
            volatile float v_ref = 3.3f;
            volatile float adc_steps = 4096.0f;
            volatile float temp_base = 27.0f;
            volatile float slope_offset = 0.706f;
            volatile float slope = 0.001721f;

            float voltage = (float)internal_temp_raw * (v_ref / adc_steps);
            internal_temp_c = temp_base - (voltage - slope_offset) / slope;

            mutex_enter_blocking(&printf_mutex); // Ensure we've got exclusive access to printf
            printf("Core 0 tick %lu (loop = %lu raw = %u c = %.2f)\n", now, loop_cnt, (unsigned int)internal_temp_raw, internal_temp_c);
            mutex_exit(&printf_mutex);
            loop_cnt = 0;
            next_tick = now + TICK_DELAY;
        }

        ++loop_cnt;

        tight_loop_contents(); // Allow other tasks to run and prevent CPU hogging
    }
}

// vim: ts=4 et nowrap