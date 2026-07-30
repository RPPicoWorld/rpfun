/**
 * @file main.c
 * @brief Using FreeRTOS on the RP2350 to demonstrate LED blinking, temperature reading, and CPU load simulation.
 * @author STM32World <lth@stm32world.com>
 * @date 2026
 *
 * Copyright (c) 2026 STM32World <lth@stm32world.com>
 *
 * Demonstrates the use of FreeRTOS on the RP2350 microcontroller, including:
 * - Blinking the onboard LED at a defined interval.
 * - Reading the internal temperature sensor via ADC and calculating the temperature in Celsius.
 * - Simulating CPU load with busy work tasks that perform floating-point calculations.
 *
 */

// Include necessary headers from the Pico SDK

#include "hardware/adc.h"    // For ADC access (if needed)
#include "hardware/clocks.h" // For clock frequency information
#include "hardware/dma.h"    // For DMA access (if needed)
#include "hardware/gpio.h"   // For GPIO control
#include "hardware/timer.h"  // Required for hardware timer access
#include "hardware/vreg.h"   // Needed for voltage scaling
#include "pico/bootrom.h"    // For flash command execution
#include "pico/multicore.h"  // For multicore support
#include "pico/stdlib.h"     // For sleep and stdio initialization

// Include standard I/O for printf
#include <stdint.h>
#include <stdio.h>

/* FreeRTOS Headers */
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#include "stats.h" // For FreeRTOS statistics monitoring

#ifndef LED_DELAY
#define LED_DELAY 500 // 500ms
#endif

#ifndef TICK_DELAY
#define TICK_DELAY 1000 // 1000ms = 1 second
#endif

// Mutex for synchronizing access to printf
static SemaphoreHandle_t printf_mutex = NULL;

volatile uint16_t internal_temp_raw = 0;
volatile float internal_temp_c = 0.0f;

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

void init_automatic_temp_sensor() {
    // Initialize the ADC hardware block
    adc_init();
    adc_set_temp_sensor_enabled(true);

    // Hardcode channel 8 configuration to match the physical QFN-80 layout
    adc_select_input(8);
}

/**
 * @brief FreeRTOS Task for LED Blinking.
 */
void led_blink_task(void *pvParameters) {
    while (1) {
        pico_toggle_led();
        vTaskDelay(pdMS_TO_TICKS(LED_DELAY));
    }
}

/**
 * @brief Heavy computation task function to simulate CPU load.
 * Instantiated multiple times across cores.
 */
void busy_work_task(void *pvParameters) {
    const char *task_name = (const char *)pvParameters;
    TickType_t xLastWakeTime = xTaskGetTickCount();

    volatile float result = 1.0001f;
    uint32_t iteration_count = 0;

    while (1) {
        // Perform a heavy floating-point loop to consume CPU cycles
        for (int i = 0; i < 600000; i++) {
            result = (result * 1.000001f) + 0.000001f;
        }

        iteration_count++;

        // if (xSemaphoreTake(printf_mutex, portMAX_DELAY) == pdTRUE) {
        //     printf("[%s | Core %d] Completed busy cycle %lu (result = %.4f)\n", task_name, get_core_num(), iteration_count, result);
        //     xSemaphoreGive(printf_mutex);
        // }

        // Brief delay (50ms) to allow StatsTask and IDLE tasks to execute
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(50));
    }
}

/**
 * @brief Entry point for Core 1 task execution in FreeRTOS.
 */
void core1_entry_task(void *pvParameters) {

    if (xSemaphoreTake(printf_mutex, portMAX_DELAY) == pdTRUE) {
        printf("Core 1: Booting...\n");
    xSemaphoreExit:
        xSemaphoreGive(printf_mutex);
    }

    // Offset Core 1 by half of TICK_DELAY (e.g., 500ms) before starting periodic execution
    vTaskDelay(pdMS_TO_TICKS(TICK_DELAY / 2));

    TickType_t xLastWakeTime = xTaskGetTickCount();
    uint32_t loop_cnt = 0;

    // Main loop for Core 1
    while (1) {
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

        if (xSemaphoreTake(printf_mutex, portMAX_DELAY) == pdTRUE) {
            printf("Core 1 tick %lu\n", now / 1000);
            xSemaphoreGive(printf_mutex);
        }

        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(TICK_DELAY));
    }
}

/**
 * @brief FreeRTOS Task for Core 0 execution.
 */
void core0_entry_task(void *pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    uint32_t loop_cnt = 0;

    while (1) {
        uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;

        // Explicitly select channel 8 for RP2354B/RP2350B variants to avoid floating pins
        adc_select_input(8);

        // 1. Instantly pull a fresh, clean hardware conversion (integer only)
        internal_temp_raw = adc_read();

        // FIX: Load values into local variables constructed at runtime to prevent
        // the compiler from compiling literal float constants inside QSPI flash memory.
        volatile float v_ref = 3.3f;
        volatile float adc_steps = 4096.0f;
        volatile float temp_base = 27.0f;
        volatile float slope_offset = 0.706f;
        volatile float slope = 0.001721f;

        float voltage = (float)internal_temp_raw * (v_ref / adc_steps);
        internal_temp_c = temp_base - (voltage - slope_offset) / slope;

        if (xSemaphoreTake(printf_mutex, portMAX_DELAY) == pdTRUE) {
            printf("Core 0 tick %lu (raw = %u c = %.2f)\n", now / 1000, (unsigned int)internal_temp_raw, internal_temp_c);
            xSemaphoreGive(printf_mutex);
        }

        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(TICK_DELAY));
    }
}

/**
 * @brief Main entry point for Core 0.
 */
int main() {

    // vreg_disable_voltage_limit(); // Disable voltage limit to allow higher voltages for overclocking

    // vreg_set_voltage(VREG_VOLTAGE_1_60); // Set voltage to 1.60V for stable overclocking
    vreg_set_voltage(VREG_VOLTAGE_1_35); // Set voltage to 1.60V for stable overclocking

    // rom_flash_enter_cmd_xip(); // Enter XIP mode for flash access

    set_sys_clock_khz(320000, true); // Set system clock to 540 MHz, true means to wait for the clock to stabilize

    int rc = pico_led_init(); // Initialize the LED GPIO

    hard_assert(rc == PICO_OK); // Ensure LED initialization was successful

    stdio_init_all(); // Initialize all standard I/O (UART, USB, etc.)

    // Explicitly override the baud rate for UART0 to 921600 for better performance with the SDK's printf implementation
    uart_set_baudrate(uart0, 921600);

    // Give UART a moment to stabilize
    sleep_ms(50);

    // Create FreeRTOS Mutex for printf synchronization
    printf_mutex = xSemaphoreCreateMutex();

    if (xSemaphoreTake(printf_mutex, portMAX_DELAY) == pdTRUE) {
        printf("\n\n\nCore 0: Booting...\n");
        printf("Running on %s at %d MHz\n",
#ifdef __riscv
               "RISC-V",
#else
               "Arm Cortex-M33",
#endif
               frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_SYS) / 1000);
        xSemaphoreGive(printf_mutex);
    }

    // Initialize the automatic temperature sensor reading via ADC
    init_automatic_temp_sensor();

    /* --- FreeRTOS Task Creation --- */

    stats_task_init(printf_mutex);

    // LED Task
    xTaskCreate(led_blink_task, "LEDTask", 256, NULL, 1, NULL);

    // Core 0 Task
    TaskHandle_t core0_handle = NULL;
    xTaskCreate(core0_entry_task, "Core0Task", 1024, NULL, 1, &core0_handle);
    vTaskCoreAffinitySet(core0_handle, (1 << 0));

    // Core 1 Task (Pinned explicitly to Core 1)
    TaskHandle_t core1_handle = NULL;
    xTaskCreate(core1_entry_task, "Core1Task", 1024, NULL, 1, &core1_handle);
    vTaskCoreAffinitySet(core1_handle, (1 << 1));

    /* --- Spawn Busy Workers --- */

    // Instance 1: Unpinned (FreeRTOS schedules on whichever core is free)
    xTaskCreate(
        busy_work_task,   // Task function
        "BusyWorker1",    // Name for FreeRTOS trace
        412,              // Stack depth (words)
        (void *)"Busy_1", // Parameter passed as task_name
        1,                // Priority
        NULL);

    // Instance 2: Unpinned (Runs alongside BusyWorker1 across Core 0/1)
    xTaskCreate(
        busy_work_task,   // Task function
        "BusyWorker2",    // Name for FreeRTOS trace
        412,              // Stack depth (words)
        (void *)"Busy_2", // Parameter passed as task_name
        1,                // Priority
        NULL);

    /* --- Start the FreeRTOS Scheduler --- */
    vTaskStartScheduler();

    while (1) {
        // Will never be reached
    }
}

// vim: ts=4 et nowrap