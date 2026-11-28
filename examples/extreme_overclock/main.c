/**
 * @file main.c
 * @brief Extreme overclocking of the RP2350
 * @author STM32World <lth@stm32world.com>
 * @date 2026
 *
 * Copyright (c) 2026 STM32World <lth@stm32world.com>
 *
 * Trying to push the RP2350 to its limits with a simple LED blink and tick example.
 *
 * A copy of the overclock example with only the changes needed to go beyond 540 MHz / 1.60 V:
 * - The clock and core voltage come from CMake (SYS_CLOCK_KHZ, CORE_VOLTAGE), so a sweep doesn't
 *   need code changes.
 * - The program still runs from the QSPI flash (XIP), but the flash clock divider is set from the
 *   target clock before the clock is raised, so the flash never runs faster than about 90 MHz
 *   (see set_xip_clkdiv()). This replaces rom_flash_enter_cmd_xip(), whose fixed clk_sys / 12 runs
 *   the flash above the usual 50 MHz 03h read limit above 600 MHz, and quad reads are much faster.
 * - clk_peri (UART, SPI, I2C) runs from the 48 MHz USB PLL instead of following clk_sys, so the
 *   UART keeps working however fast the cores run.
 * - The regulator gets time to settle after a big voltage step, before the clock is raised.
 *
 */

// Include necessary headers from the Pico SDK

#include "hardware/adc.h"    // For ADC access (if needed)
#include "hardware/clocks.h" // For clock frequency information
#include "hardware/dma.h"    // For DMA access (if needed)
#include "hardware/flash.h"  // For flash_flush_cache()
#include "hardware/gpio.h"  // For GPIO control
#include "hardware/structs/qmi.h" // QMI (flash interface) registers, for the flash clock divider
#include "hardware/sync.h"  // save_and_disable_interrupts()
#include "hardware/timer.h" // Required for hardware timer access
#include "hardware/vreg.h"  // Needed for voltage scaling
#include "pico/bootrom.h"   // For flash command execution
#include "pico/multicore.h" // For multicore support
#include "pico/stdlib.h"    // For sleep and stdio initialization

// Include standard I/O for printf
#include <stdint.h>
#include <stdio.h>

// Clock and core voltage, normally set from CMake (-DSYS_CLOCK_KHZ=... -DCORE_VOLTAGE=...)
#ifndef SYS_CLOCK_KHZ
#define SYS_CLOCK_KHZ 540000
#endif

#ifndef CORE_VOLTAGE
#define CORE_VOLTAGE VREG_VOLTAGE_1_60
#endif

// Flash (XIP) clock: clk_sys / XIP_CLKDIV, at most XIP_MAX_KHZ. Measured on the Streamline board
// at 150, 540 and 624 MHz: with read delay 4 the quad reads are correct at every divider from 5 to
// 10, and the smallest working read delay only depends on the flash clock (about 90 MHz needs 1,
// 104 MHz needs 2, 124 MHz needs 3). So 90 MHz with read delay 4 leaves margin on both sides.
#define XIP_MAX_KHZ 90000
#define XIP_CLKDIV_MIN 3 // The boot setup's divider, never go faster than that
#define XIP_CLKDIV_CALC ((SYS_CLOCK_KHZ + XIP_MAX_KHZ - 1) / XIP_MAX_KHZ) // Round up
#define XIP_CLKDIV (XIP_CLKDIV_CALC > XIP_CLKDIV_MIN ? XIP_CLKDIV_CALC : XIP_CLKDIV_MIN)
#define XIP_RXDELAY 4 // Read data sample delay, in half system clock cycles

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
 * @brief Sets the flash (XIP) clock divider and read delay for the target system clock.
 *
 * Keeps the boot time quad read setup and only changes the clock divider and read delay, before
 * the system clock is raised (the setting also reads correctly at the 150 MHz boot clock). Runs
 * from RAM, as the flash can't be read while its timing changes.
 */
static void __no_inline_not_in_flash_func(set_xip_clkdiv)(void) {
    uint32_t ints = save_and_disable_interrupts();
    uint32_t timing = qmi_hw->m[0].timing;
    timing &= ~(QMI_M0_TIMING_CLKDIV_BITS | QMI_M0_TIMING_RXDELAY_BITS);
    timing |= (XIP_CLKDIV << QMI_M0_TIMING_CLKDIV_LSB) | (XIP_RXDELAY << QMI_M0_TIMING_RXDELAY_LSB);
    qmi_hw->m[0].timing = timing;
    flash_flush_cache(); // Drop anything cached with the old timing
    restore_interrupts(ints);
}

/**
 * @brief Main entry point for Core 0.
 */
int main() {

    vreg_disable_voltage_limit(); // Disable voltage limit to allow higher voltages for overclocking

    vreg_set_voltage(CORE_VOLTAGE); // Core voltage, set from CMake (up to VREG_VOLTAGE_3_30 with the limit disabled)

    sleep_ms(10); // Give the regulator time to settle, a step to 2-3 V takes a while

    set_xip_clkdiv(); // Flash clock for the target clock (clk_sys / XIP_CLKDIV), before raising the clock

    set_sys_clock_khz(SYS_CLOCK_KHZ, true); // System clock, set from CMake, true means to wait for the clock to stabilize

    // set_sys_clock_khz() runs clk_peri straight from clk_sys, so the UART would see the full core
    // clock. Run it from the 48 MHz USB PLL instead, which the UART is happy with at any core clock.
    clock_configure_undivided(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLKSRC_PLL_USB, USB_CLK_HZ);

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
    printf("Flash clock: clk_sys / %d = %d MHz\n", XIP_CLKDIV, SYS_CLOCK_KHZ / 1000 / XIP_CLKDIV);
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
