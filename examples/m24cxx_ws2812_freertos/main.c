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

// Set target memory topology before including library header
#define M24CXX_MODEL M24M01X4
#include "m24cxx.h" // For M24CXX EEPROM access

#define LED_DELAY 500   // 500ms
#define TICK_DELAY 1000 // 1000ms = 1 second

#define NUM_LEDS 64
#define WS2812_PIN 12
#define IS_RGBW false

#define WS2812_FRAME_DELAY 20    // 20ms update interval (~50 FPS)
#define PATTERN_SWAP_DELAY 10000 // Switch pattern every 10 seconds

// Max brightness factor (0 = off, 255 = 100% full brightness)
#define MAX_BRIGHTNESS 64 // ~25% brightness

#define SAFE_PRINTF(fmt, ...)                                        \
    do {                                                             \
        if (xSemaphoreTake(printf_mutex, portMAX_DELAY) == pdTRUE) { \
            printf(fmt, ##__VA_ARGS__);                              \
            xSemaphoreGive(printf_mutex);                            \
        }                                                            \
    } while (0)

// Define a structure to represent a WS2812 pixel with its index and RGB color components
struct ws2812_pixel_t {
    uint16_t index; // Index of the LED in the strip (0 to NUM_LEDS-1)
    uint8_t red;    // Red component (0-255)
    uint8_t green;  // Green component (0-255)
    uint8_t blue;   // Blue component (0-255)
};

m24cxx_t m24cxx_dev;

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

// Handle for m24cxx demo task
TaskHandle_t m24cxx_demo_task_handle = NULL;

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
 * @brief IEEE 802.3 CRC32 calculation.
 */
uint32_t calculate_crc32(const uint8_t *data, size_t length, uint32_t previous_crc) {
    uint32_t crc = ~previous_crc;
    for (size_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320;
            else
                crc >>= 1;
        }
    }
    return ~crc;
}

/**
 * @brief Demo function to write data to the M24CXX EEPROM.
 * @return uint32_t Returns the calculated CRC32 of the written data.
 */
uint32_t m24cxx_demo_write() {

    uint8_t tx_buf[M24CXX_WRITE_PAGE_SIZE];

    // --- WRITE PASS ---
    SAFE_PRINTF("Writing %lu kB in %d-byte blocks via m24cxx...\n", M24CXX_SIZE / 1024, M24CXX_WRITE_PAGE_SIZE);

    uint32_t write_crc = 0;
    uint32_t start_write_tick = xTaskGetTickCount();
    uint32_t bytes_written = 0;

    const uint32_t bytes_per_led = M24CXX_SIZE / NUM_LEDS; // 524,288 / 64 = 8,192 bytes per LED

    for (uint32_t addr = 0; addr < M24CXX_SIZE; addr += M24CXX_WRITE_PAGE_SIZE) {

        for (uint32_t i = 0; i < M24CXX_WRITE_PAGE_SIZE; i++) {
            tx_buf[i] = (uint8_t)((addr + i) ^ 0xA5);
        }

        write_crc = calculate_crc32(tx_buf, M24CXX_WRITE_PAGE_SIZE, write_crc);

        if (m24cxx_write(&m24cxx_dev, addr, tx_buf, M24CXX_WRITE_PAGE_SIZE) != M24CXX_OK) {
            SAFE_PRINTF("\n[ERROR] m24cxx_write failed at 0x%05lX\n", addr);
            return 0;
        }

        bytes_written += M24CXX_WRITE_PAGE_SIZE;

        // Only update the LED when the LAST byte of a full 8 kB block has finished
        if (bytes_written % bytes_per_led == 0) {
            uint16_t completed_led = (bytes_written / bytes_per_led) - 1;

            struct ws2812_pixel_t pixel = {
                .index = completed_led,
                .red = 0,
                .green = 0,
                .blue = 20
            };
            xQueueSend(ws2812_queue, &pixel, portMAX_DELAY);

            if (ws2812_refresh_task_handle != NULL) {
                xTaskNotifyGive(ws2812_refresh_task_handle);
            }
        }
    }

    uint32_t write_elapsed_ms = (xTaskGetTickCount() - start_write_tick) * portTICK_PERIOD_MS;
    SAFE_PRINTF("Write Complete in %lu ms | CRC32: 0x%08lX\n\n", write_elapsed_ms, write_crc);

    return write_crc;
}

/**
 * @brief Demo function to read data from the M24CXX EEPROM.
 * @return uint32_t Returns the calculated CRC32 of the read data.
 */
uint32_t m24cxx_demo_read() {

    uint8_t rx_buf[M24CXX_READ_PAGE_SIZE];

    // --- READ PASS ---
    SAFE_PRINTF("Reading %lu kB in %d-byte blocks via m24cxx...\n", M24CXX_SIZE / 1024, M24CXX_READ_PAGE_SIZE);

    uint32_t read_crc = 0;
    uint32_t start_read_tick = xTaskGetTickCount();
    uint32_t bytes_read = 0;

    const uint32_t bytes_per_led = M24CXX_SIZE / NUM_LEDS; // 8,192 bytes per LED

    for (uint32_t addr = 0; addr < M24CXX_SIZE; addr += M24CXX_READ_PAGE_SIZE) {

        if (m24cxx_read(&m24cxx_dev, addr, rx_buf, M24CXX_READ_PAGE_SIZE) != M24CXX_OK) {
            SAFE_PRINTF("\n[ERROR] m24cxx_read failed at 0x%05lX\n", addr);
            return 0;
        }

        read_crc = calculate_crc32(rx_buf, M24CXX_READ_PAGE_SIZE, read_crc);

        bytes_read += M24CXX_READ_PAGE_SIZE;

        // Only update the LED when the LAST byte of a full 8 kB block has finished
        if (bytes_read % bytes_per_led == 0) {
            uint16_t completed_led = (bytes_read / bytes_per_led) - 1;

            struct ws2812_pixel_t pixel = {
                .index = completed_led,
                .red = 0,
                .green = 20,
                .blue = 0
            };
            xQueueSend(ws2812_queue, &pixel, portMAX_DELAY);

            if (ws2812_refresh_task_handle != NULL) {
                xTaskNotifyGive(ws2812_refresh_task_handle);
            }
        }
    }

    uint32_t read_elapsed_ms = (xTaskGetTickCount() - start_read_tick) * portTICK_PERIOD_MS;
    SAFE_PRINTF("Read Complete in %lu ms | CRC32: 0x%08lX\n\n", read_elapsed_ms, read_crc);

    return read_crc;
}

/**
 * @brief Demo function to erase data from the M24CXX EEPROM.
 */
void m24cxx_demo_erase() {

    // --- ERASE PASS ---
    SAFE_PRINTF("Erasing %lu kB in %d-byte blocks via m24cxx_erase...\n", M24CXX_SIZE / 1024, M24CXX_WRITE_PAGE_SIZE);

    uint32_t start_erase_tick = xTaskGetTickCount();
    uint32_t bytes_erased = 0;

    const uint32_t bytes_per_led = M24CXX_SIZE / NUM_LEDS; // 8,192 bytes per LED

    for (uint32_t addr = 0; addr < M24CXX_SIZE; addr += M24CXX_WRITE_PAGE_SIZE) {

        //erase_crc = calculate_crc32(ff_buf, M24CXX_WRITE_PAGE_SIZE, erase_crc);

        if (m24cxx_erase(&m24cxx_dev, addr, M24CXX_WRITE_PAGE_SIZE) != M24CXX_OK) {
            SAFE_PRINTF("\n[ERROR] m24cxx_erase failed at 0x%05lX\n", addr);
            return;
        }

        bytes_erased += M24CXX_WRITE_PAGE_SIZE;

        // Only turn OFF the LED when the LAST byte of a full 8 kB block has finished
        if (bytes_erased % bytes_per_led == 0) {
            uint16_t completed_led = (bytes_erased / bytes_per_led) - 1;

            struct ws2812_pixel_t pixel = {
                .index = completed_led,
                .red = 0,
                .green = 0,
                .blue = 0
            };
            xQueueSend(ws2812_queue, &pixel, portMAX_DELAY);

            if (ws2812_refresh_task_handle != NULL) {
                xTaskNotifyGive(ws2812_refresh_task_handle);
            }
        }
    }

    uint32_t erase_elapsed_ms = (xTaskGetTickCount() - start_erase_tick) * portTICK_PERIOD_MS;
    SAFE_PRINTF("Erase Complete in %lu ms\n\n", erase_elapsed_ms);

    return;
}

void m24cxx_demo_task(void *pvParameters) {

    uint8_t rx_buf[M24CXX_READ_PAGE_SIZE];

    SAFE_PRINTF("Starting M24CXX demo task...\n");

    for (int i = 0; i < NUM_LEDS; i++) {
        struct ws2812_pixel_t pixel = {
            .index = i,
            .red = 20,
            .green = 0,
            .blue = 0
        };
        xQueueSend(ws2812_queue, &pixel, portMAX_DELAY);
    }

    // Signal refresh task to trigger DMA transfer once full frame is queued
    if (ws2812_refresh_task_handle != NULL) {
        xTaskNotifyGive(ws2812_refresh_task_handle);
    }

    SAFE_PRINTF("\n=== Starting %lu kB (%s) Memory Benchmark ===\n", M24CXX_SIZE / 1024, M24CXX_TYPE);

    uint32_t write_crc = m24cxx_demo_write();

    uint32_t read_crc = m24cxx_demo_read();

    if (write_crc != read_crc) {
        SAFE_PRINTF("[ERROR] CRC32 mismatch: Write CRC = 0x%08lX, Read CRC = 0x%08lX\n", write_crc, read_crc);
    } else {
        SAFE_PRINTF("CRC32 match: 0x%08lX\n", write_crc);
    }

    m24cxx_demo_erase();

    SAFE_PRINTF("Ending M24CXX demo task...\n");

    // Correct way to exit a FreeRTOS task:
    vTaskDelete(NULL);
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

        // Check if task handle exists and if the task has entered deleted state
        if (m24cxx_demo_task_handle != NULL) {
            eTaskState state = eTaskGetState(m24cxx_demo_task_handle);
            if (state == eDeleted) {
                m24cxx_demo_task_handle = NULL; // Safe to clear now
            }
        }

        if (m24cxx_demo_task_handle == NULL) {

            if (xSemaphoreTake(printf_mutex, portMAX_DELAY) == pdTRUE) {
                printf("M24CXX task is NOT running - starting...\n");
                xSemaphoreGive(printf_mutex);
            }

            // Create the M24CXX Demo Task on Core 1
            xTaskCreate(
                m24cxx_demo_task,
                "M24CXXDemoTask",
                configMINIMAL_STACK_SIZE + 1024,
                NULL,
                2,
                &m24cxx_demo_task_handle);
        }

        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(TICK_DELAY));
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

    // Initialize I2C0 at 1 MHz (Fast Mode)
    i2c_init(i2c_default, 1000 * 1000);
    gpio_set_function(PICO_DEFAULT_I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(PICO_DEFAULT_I2C_SCL_PIN, GPIO_FUNC_I2C);
    //  Make the I2C pins available to picotool
    //  bi_decl(bi_2pins_with_func(PICO_DEFAULT_I2C_SDA_PIN, PICO_DEFAULT_I2C_SCL_PIN, GPIO_FUNC_I2C));

    printf("\nI2C Bus Scan\n");
    printf("   0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F\n");
    for (int addr = 0; addr < (1 << 7); ++addr) {
        if (addr % 16 == 0) {
            printf("%02x ", addr);
        }

        // Skip over any reserved addresses.
        int ret;
        uint8_t rxdata;
        ret = i2c_read_blocking(i2c_default, addr, &rxdata, 1, false);

        printf(ret < 0 ? "." : "*");
        printf(addr % 16 == 15 ? "\n" : "  ");
    }
    printf("Done.\n");

    // Initialize m24cxx library instance
    m24cxx_init(&m24cxx_dev, i2c_default, 0x50, -1); // No WP pin used in this example

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

    // // Create the WS2812 Animation Demo Task
    // xTaskCreate(
    //     ws2812_demo_task,
    //     "WSDemoTask",
    //     configMINIMAL_STACK_SIZE + 256,
    //     NULL,
    //     2,
    //     &ws2812_demo_task_handle);

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