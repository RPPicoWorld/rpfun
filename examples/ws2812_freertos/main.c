/**
 * @file main.c
 * @brief Using FreeRTOS to drive a WS2812 LED matrix on the RP2350 microcontroller.
 * @author STM32World <lth@stm32world.com>
 * @date 2026
 *
 * Copyright (c) 2026 STM32World <lth@stm32world.com>
 *
 * Demonstrates the use of FreeRTOS on the RP2350 microcontroller, including:
 * - Multicore task management
 * - WS2812 LED matrix control using PIO and DMA
 * - Periodic tasks with timers
 *
 */

// Include necessary headers from the Pico SDK
#include "hardware/adc.h"             // For ADC access (if needed)
#include "hardware/clocks.h"          // For clock frequency information
#include "hardware/dma.h"             // For DMA access (if needed)
#include "hardware/gpio.h"            // For GPIO control
#include "hardware/structs/sysinfo.h" // For accessing SYSINFO registers
#include "hardware/timer.h"           // Required for hardware timer access
#include "hardware/vreg.h"            // Needed for voltage scaling
#include "pico/bootrom.h"             // For flash command execution
#include "pico/multicore.h"           // For multicore support
#include "pico/stdlib.h"              // For sleep and stdio initialization

// FreeRTOS Headers
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include "timers.h"

// Include standard I/O for printf
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "ws2812.pio.h" // For WS2812 LED control

#include "stats.h" // For FreeRTOS statistics monitoring

#define LED_DELAY 500   // 500ms
#define TICK_DELAY 1000 // 1000ms = 1 second

#define NUM_LEDS 64
#define WS2812_PIN 29
#define IS_RGBW false

#define WS2812_FRAME_DELAY 20    // 20ms update interval (~50 FPS)
#define PATTERN_SWAP_DELAY 10000 // Switch pattern every 10 seconds

// Max brightness factor (0 = off, 255 = 100% full brightness)
#define MAX_BRIGHTNESS 64 // ~25% brightness

// Define a structure to represent a WS2812 pixel with its index and RGB color components
struct ws2812_pixel_t {
    uint16_t index; // Index of the LED in the strip (0 to NUM_LEDS-1)
    uint8_t red;    // Red component (0-255)
    uint8_t green;  // Green component (0-255)
    uint8_t blue;   // Blue component (0-255)
};

// --- DMA Globals ---
static uint32_t led_buffer[NUM_LEDS]; // Buffer to hold the 24-bit GRB values for each LED, aligned to 32 bits for DMA transfer
static int dma_chan = -1;             // DMA channel used for transferring led_buffer to the PIO state machine

// Mutex for synchronizing access to printf
SemaphoreHandle_t printf_mutex = NULL;

// Handle for led task
TaskHandle_t led_task_handle = NULL;

// Handle for the notification task
TaskHandle_t ws2812_refresh_task_handle = NULL; // We need this handle to send notifications to the task

// Handle for ws2812 pixel set led
TaskHandle_t ws2812_set_led_task_handle = NULL;

// Handle for ws2812 demo
TaskHandle_t ws2812_demo_task_handle = NULL;

// Queue handle for WS2812 pixel data
QueueHandle_t ws2812_queue = NULL;

// Timer handle for LED blinking
TimerHandle_t led_timer_handle = NULL;

/**
 * @brief Initializes the default LED GPIO pin for output.
 * @return int Returns PICO_OK on success, or an error code on failure.
 */
int pico_led_init(void) {
    gpio_init(PICO_DEFAULT_LED_PIN);              // The LED pin is defined in the board header as PICO_DEFAULT_LED_PIN
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT); // Set the LED pin as an output
    gpio_put(PICO_DEFAULT_LED_PIN, 1);            // Drive pin HIGH immediately
    return PICO_OK;
}

/**
 * @brief Toggles the state of the default LED.
 */
void pico_toggle_led() {
    gpio_xor_mask64(((uint64_t)1 << PICO_DEFAULT_LED_PIN)); // Toggle the LED pin state using XOR operation
}

/**
 * @brief Initialize the adc
 */
void init_automatic_temp_sensor() {
    // Initialize the ADC hardware block
    adc_init();
    adc_set_temp_sensor_enabled(true);

    // Hardcode channel 8 configuration to match the physical QFN-80 layout
    adc_select_input(8);
}

/**
 * @brief Configures a DMA channel to stream led_buffer to PIO0 SM0.
 */
void ws2812_dma_init(PIO pio, uint sm) {
    dma_chan = dma_claim_unused_channel(true);

    dma_channel_config c = dma_channel_get_default_config(dma_chan);

    // 32-bit transfer word size
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);

    // Increment source address (read through buffer), hold destination fixed (PIO FIFO)
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);

    // Pace transfers based on PIO0 SM0 TX FIFO availability
    channel_config_set_dreq(&c, pio_get_dreq(pio, sm, true));

    dma_channel_configure(
        dma_chan,
        &c,
        &pio->txf[sm], // Destination: PIO State Machine TX FIFO
        led_buffer,    // Source: RAM buffer
        NUM_LEDS,      // Number of words to transfer
        false          // Do not start immediately
    );
}

/**
 * @brief Timer callback function.
 * @param xTimer Timer handle.
 */
void vTimerCallback(TimerHandle_t xTimer) {
    if (xTimer == led_timer_handle) {
        xTaskNotifyGive(led_task_handle); // Signal the led task to toggle the LED
    }
}

/**
 * @brief FreeRTOS Task for LED Blinking.
 */
void led_blink_task(void *pvParameters) {
    while (1) {

        // Wait indefinitely (portMAX_DELAY) for the notification
        uint32_t ulCount = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        if (ulCount > 0) {
            pico_toggle_led();
        }
    }
}

/**
 * @brief Trigger a DMA transfer to refresh the WS2812 LED strip.
 */
void ws2812_refresh_task(void *pvParameters) {
    while (true) {
        uint32_t ulCount = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        if (ulCount > 0) {

            // Wait ONLY if a previous transfer is active
            while (dma_channel_is_busy(dma_chan)) {
                vTaskDelay(1); // Yield execution to let DMA finish
            }

            // Start DMA transaction
            dma_channel_set_read_addr(dma_chan, led_buffer, true);
        }
    }
}

/**
 * @brief FreeRTOS Task to receive pixel data from the queue and update the led_buffer.
 */
void ws2812_queue_receive_task(void *pvParameters) {
    while (1) {
        struct ws2812_pixel_t pixel;

        if (xQueueReceive(ws2812_queue, &pixel, portMAX_DELAY) == pdPASS) {
            // Apply brightness scaling
            uint8_t r = (pixel.red * MAX_BRIGHTNESS) >> 8;
            uint8_t g = (pixel.green * MAX_BRIGHTNESS) >> 8;
            uint8_t b = (pixel.blue * MAX_BRIGHTNESS) >> 8;

            // Pack 24-bit GRB value
            uint32_t grb = ((uint32_t)g << 16) | ((uint32_t)r << 8) | (uint32_t)b;

            // Align to upper 24 bits for the PIO state machine
            if (pixel.index < NUM_LEDS) {
                led_buffer[pixel.index] = grb << 8u;
            }
        }
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

        // Read ADC channel 8 (RP2350 internal temp sensor)
        uint16_t internal_temp_raw = adc_read();

        // Perform math using 64-bit literals (LL) to avoid 32-bit overflow:
        // Voltage in uV = (raw * 3,300,000) / 4096 => raw * 805.6640625
        int64_t uv = ((int64_t)internal_temp_raw * 3300000LL) / 4096LL;

        // Compute temperature in tenths of a degree C (e.g. 409 = 40.9 °C)
        // T = 27.0 - (V_uv - 706000) / 1721
        int32_t temp_deci_c = 270 - (int32_t)(((uv - 706000LL) * 10LL) / 1721LL);

        int32_t rem = temp_deci_c % 10;

        if (xSemaphoreTake(printf_mutex, portMAX_DELAY) == pdTRUE) {
            printf("Core 0 tick %lu (r = %u t = %ld.%ld °C)\n", now / 1000, internal_temp_raw, temp_deci_c / 10, rem < 0 ? -rem : rem);
            xSemaphoreGive(printf_mutex);
        }

        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(TICK_DELAY));
    }
}

/**
 * @brief Entry point for Core 1 task execution in FreeRTOS.
 */
void core1_entry_task(void *pvParameters) {

    if (xSemaphoreTake(printf_mutex, portMAX_DELAY) == pdTRUE) {
        printf("Core 1: Booting...\n");
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
 * @brief Original FreeRTOS Demo Task mapped to 8x8 Serpentine Matrix
 */
void ws2812_demo_task(void *pvParameters) {

    TickType_t xLastWakeTime = xTaskGetTickCount();
    uint8_t pattern_mode = 0;
    uint16_t state_counter = 0;
    uint32_t pattern_swap_ticks = pdMS_TO_TICKS(PATTERN_SWAP_DELAY);
    TickType_t next_pattern_time = xLastWakeTime + pattern_swap_ticks;

    while (1) {
        TickType_t now = xTaskGetTickCount();

        // Switch animation pattern every 10 seconds
        if (now >= next_pattern_time) {
            pattern_mode = (pattern_mode + 1) % 4;
            state_counter = 0;
            next_pattern_time = now + pattern_swap_ticks;
        }

        // Generate animation frame
        switch (pattern_mode) {
        case 0: { // Rainbow Wave
            for (int i = 0; i < NUM_LEDS; i++) {
                uint8_t pos = (i * 256 / NUM_LEDS + state_counter) & 0xFF;
                pos = 255 - pos;
                uint8_t r, g, b;
                if (pos < 85) {
                    r = 255 - pos * 3;
                    g = 0;
                    b = pos * 3;
                } else if (pos < 170) {
                    pos -= 85;
                    r = 0;
                    g = pos * 3;
                    b = 255 - pos * 3;
                } else {
                    pos -= 170;
                    r = pos * 3;
                    g = 255 - pos * 3;
                    b = 0;
                }
                struct ws2812_pixel_t pixel = { .index = i, .red = r, .green = g, .blue = b };
                xQueueSend(ws2812_queue, &pixel, portMAX_DELAY);
            }
            state_counter += 2;
            break;
        }

        case 1: { // Pixel Chaser with Fade Tail
            uint16_t head = state_counter % NUM_LEDS;
            for (int i = 0; i < NUM_LEDS; i++) {
                struct ws2812_pixel_t pixel = { .index = i, .red = 0, .green = 0, .blue = 0 };
                if (i == head) {
                    pixel.red = 255;
                    pixel.green = 255;
                    pixel.blue = 255;
                } else if (i == (head + NUM_LEDS - 1) % NUM_LEDS) {
                    pixel.blue = 150;
                } else if (i == (head + NUM_LEDS - 2) % NUM_LEDS) {
                    pixel.blue = 40;
                }
                xQueueSend(ws2812_queue, &pixel, portMAX_DELAY);
            }
            state_counter++;
            break;
        }

        case 2: { // Color Wipe
            uint16_t fill_len = (state_counter / 2) % (NUM_LEDS + 1);
            uint8_t color_idx = (state_counter / (NUM_LEDS * 2)) % 3;

            for (int i = 0; i < NUM_LEDS; i++) {
                struct ws2812_pixel_t pixel = { .index = i, .red = 0, .green = 0, .blue = 0 };
                if (i < fill_len) {
                    if (color_idx == 0)
                        pixel.red = 255;
                    else if (color_idx == 1)
                        pixel.green = 255;
                    else
                        pixel.blue = 255;
                }
                xQueueSend(ws2812_queue, &pixel, portMAX_DELAY);
            }
            state_counter++;
            break;
        }

        case 3: { // Breathing Pulse
            uint8_t brightness;
            uint8_t phase = state_counter & 0xFF;
            brightness = (phase < 128) ? (phase * 2) : ((255 - phase) * 2);

            for (int i = 0; i < NUM_LEDS; i++) {
                struct ws2812_pixel_t pixel = {
                    .index = i,
                    .red = brightness,
                    .green = brightness / 2,
                    .blue = 0
                };
                xQueueSend(ws2812_queue, &pixel, portMAX_DELAY);
            }
            state_counter += 2;
            break;
        }
        }

        // Signal refresh task to trigger DMA transfer once full frame is queued
        if (ws2812_refresh_task_handle != NULL) {
            xTaskNotifyGive(ws2812_refresh_task_handle);
        }

        // Target ~50 FPS (20ms interval)
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(WS2812_FRAME_DELAY));
    }
}

/**
 * @brief Main entry point for the FreeRTOS application on the RP2350.
 * Initializes hardware, sets up tasks, and starts the FreeRTOS scheduler.
 * @return int Returns 0 on successful execution (never reached due to scheduler).
 */
int main() {

    // vreg_disable_voltage_limit(); // Disable voltage limit to allow higher voltages for overclocking

    // vreg_set_voltage(VREG_VOLTAGE_1_60); // Set voltage to 1.60V for stable overclocking up to 540 MHz - stable for 540 MHz operation
    // vreg_set_voltage(VREG_VOLTAGE_1_35); // Set voltage to 1.35V for stable overclocking - stable for 340 MHz operation

    // rom_flash_enter_cmd_xip(); // Enter XIP mode for flash access

    // set_sys_clock_khz(540000, true); // Set system clock to 340 MHz, true means to wait for the clock to stabilize

    int rc = pico_led_init(); // Initialize the LED GPIO

    hard_assert(rc == PICO_OK); // Ensure LED initialization was successful

    stdio_init_all(); // Initialize all standard I/O (UART, USB, etc.)

    // Explicitly override the baud rate for UART0 to 921600 for better performance with the SDK's printf implementation
    uart_set_baudrate(uart0, 921600);

    // Give UART a moment to stabilize
    sleep_ms(10);

    // Create FreeRTOS Mutex for printf synchronization
    printf_mutex = xSemaphoreCreateMutex();

    if (xSemaphoreTake(printf_mutex, portMAX_DELAY) == pdTRUE) {
        // Read the revision field (bits 31:28) from SYSINFO_CHIP_ID
        uint8_t stepping_rev = (sysinfo_hw->chip_id & SYSINFO_CHIP_ID_REVISION_BITS) >> SYSINFO_CHIP_ID_REVISION_LSB;

        printf("\n\n\nCore 0: Booting...\n");
        printf("Running on %s (Stepping A%u) at %d MHz\n",
#ifdef __riscv
               "RISC-V",
#else
               "Arm Cortex-M33",
#endif
               stepping_rev,
               frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_SYS) / 1000);

        xSemaphoreGive(printf_mutex);
    }

    // Initialize the automatic temperature sensor reading via ADC
    init_automatic_temp_sensor();

    // Select PIO instance and state machine
    PIO pio = pio0;
    int sm = 0;
    uint offset = pio_add_program(pio, &ws2812_program);

    // Initialize PIO for GPIO 29 at 800kHz target frequency
    ws2812_program_init(pio, sm, offset, WS2812_PIN, 800000, IS_RGBW);

    // Initialize DMA hardware for PIO0 State Machine 0
    ws2812_dma_init(pio, sm);

    // Create FreeRTOS Queue for WS2812 pixel updates (holds up to full LED count)
    ws2812_queue = xQueueCreate(NUM_LEDS, sizeof(struct ws2812_pixel_t));
    hard_assert(ws2812_queue != NULL);

    /* --- FreeRTOS Task Creation --- */

    stats_task_init(printf_mutex);

    // Create the LED timer for periodic toggling
    led_timer_handle = xTimerCreate(
        "LEDTimer",               // Text name
        pdMS_TO_TICKS(LED_DELAY), // Timer period (500ms)
        pdTRUE,                   // pdTRUE = Auto-Reload Timer
        (void *)1,                // Timer ID
        vTimerCallback            // Callback function
    );

    // Start the LED timer
    xTimerStart(led_timer_handle, 0);

    // LED Task
    xTaskCreate(
        led_blink_task,
        "LEDTask",
        126,
        NULL,
        1,
        &led_task_handle);

    xTaskCreate(
        ws2812_queue_receive_task,
        "WSQueueTask",
        configMINIMAL_STACK_SIZE,
        NULL,
        3,
        NULL);

    xTaskCreate(
        ws2812_refresh_task,
        "RefreshTask",
        configMINIMAL_STACK_SIZE,
        NULL,
        2,
        &ws2812_refresh_task_handle);

    // Create the WS2812 Animation Demo Task
    xTaskCreate(
        ws2812_demo_task,
        "WSDemoTask",
        configMINIMAL_STACK_SIZE + 256,
        NULL,
        2,
        &ws2812_demo_task_handle);

    // Core 0 Task
    TaskHandle_t core0_handle = NULL;
    xTaskCreate(
        core0_entry_task,
        "Core0Task",
        256,
        NULL,
        3,
        &core0_handle);
    vTaskCoreAffinitySet(core0_handle, (1 << 0)); // Pin Core 0 Task to Core 0

    // Core 1 Task
    TaskHandle_t core1_handle = NULL;
    xTaskCreate(
        core1_entry_task,
        "Core1Task",
        256,
        NULL,
        3,
        &core1_handle);
    vTaskCoreAffinitySet(core1_handle, (1 << 1)); // Pin Core 1 Task to Core 1

    /* --- Start the FreeRTOS Scheduler --- */
    vTaskStartScheduler();

    while (1) {
        (void)0; // Should never be reached but will do bugger all
    }
}

// vim: ts=4 et nowrap autoindent