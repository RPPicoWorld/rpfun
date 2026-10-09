/**
 * @file main.c
 * @brief I2C M24M01 x 4 demo
 * @author STM32World <lth@stm32world.com>
 * @date 2026
 *
 * Copyright (c) 2026 STM32World <lth@stm32world.com>
 *
 * Enabling I2C functionality on the Raspberry Pi Pico using the Pico SDK's I2C stack, which is
 * compatible with both ARM and RISC-V cores. This example demonstrates:
 * Creating an I2C master
 * Scanning the I2C bus for connected devices
 *
 */

// Include necessary headers from the Pico SDK

#include "hardware/clocks.h"  // For clock frequency information
#include "hardware/gpio.h"    // For GPIO control
#include "hardware/i2c.h"     // For I2C communication
#include "hardware/timer.h"   // Required for hardware timer access
#include "hardware/vreg.h"    // Needed for voltage scaling
#include "pico/binary_info.h" // For binary information macros
#include "pico/stdlib.h"      // For sleep and stdio initialization

#include <stdio.h>
#include <string.h>

// Set target memory topology before including library header
#define M24CXX_MODEL M24M01X4
#include "m24cxx.h"

#define LED_DELAY 500      // 500ms
#define BLINK_MOUNTED 1000 // 1s
#define TICK_DELAY 1000

// Function prototypes
int pico_led_init(void);
void pico_toggle_led(void);
bool on_timer_tick(struct repeating_timer *t);
void universal_tick_init();

// Volatile variable to mimic STM32's uwTick
static volatile uint32_t systick = 0;
volatile uint32_t blink_interval_ms = LED_DELAY;

// I2C reserves some addresses for special purposes. We exclude these from the scan.
// These are any addresses of the form 000 0xxx or 111 1xxx
bool reserved_addr(uint8_t addr) {
    return (addr & 0x78) == 0 || (addr & 0x78) == 0x78;
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
 * @brief Runs write, read, and CRC verification pass over configurable test size.
 * @param dev Pointer to initialized m24cxx device struct
 * @param test_size_bytes Total bytes to benchmark (e.g. 16384 for fast test, M24CXX_SIZE for full 512 kB run)
 */
void run_m24cxx_demo(m24cxx_t *dev, uint32_t test_size_bytes) {
    uint8_t tx_buf[M24CXX_WRITE_PAGE_SIZE];
    uint8_t rx_buf[M24CXX_READ_PAGE_SIZE];

    if (test_size_bytes > M24CXX_SIZE) {
        test_size_bytes = M24CXX_SIZE;
    }

    printf("\n=== Starting %lu kB (%s) Memory Benchmark ===\n", test_size_bytes / 1024, M24CXX_TYPE);

    // --- WRITE PASS ---
    printf("Writing %lu kB in %d-byte blocks via m24cxx...\n", test_size_bytes / 1024, M24CXX_WRITE_PAGE_SIZE);

    uint32_t write_crc = 0;
    uint32_t start_write_tick = systick;
    uint32_t bytes_written = 0;

    for (uint32_t addr = 0; addr < test_size_bytes; addr += M24CXX_WRITE_PAGE_SIZE) {
        for (uint32_t i = 0; i < M24CXX_WRITE_PAGE_SIZE; i++) {
            tx_buf[i] = (uint8_t)((addr + i) ^ 0xA5);
        }

        write_crc = calculate_crc32(tx_buf, M24CXX_WRITE_PAGE_SIZE, write_crc);

        if (m24cxx_write(dev, addr, tx_buf, M24CXX_WRITE_PAGE_SIZE) != M24CXX_OK) {
            printf("\n[ERROR] m24cxx_write failed at 0x%05lX\n", addr);
            return;
        }

        bytes_written += M24CXX_WRITE_PAGE_SIZE;

        // Print a dot for every 1 kB (1024 bytes)
        if (bytes_written % 1024 == 0) {
            printf(".");
            fflush(stdout);

            // Wrap line and show progress every 16 kB
            if ((bytes_written / 1024) % 16 == 0) {
                printf(" [%lu kB / %lu ms]\n", bytes_written / 1024, systick - start_write_tick);
            }
        }
    }

    if ((bytes_written / 1024) % 16 != 0) {
        printf("\n");
    }

    uint32_t write_elapsed_ms = systick - start_write_tick;
    printf("Write Complete in %lu ms | CRC32: 0x%08lX\n\n", write_elapsed_ms, write_crc);

    // --- READ PASS ---
    printf("Reading back %lu kB and verifying CRC...\n", test_size_bytes / 1024);

    uint32_t read_crc = 0;
    uint32_t start_read_tick = systick;
    uint32_t bytes_read = 0;

    for (uint32_t addr = 0; addr < test_size_bytes; addr += M24CXX_READ_PAGE_SIZE) {
        memset(rx_buf, 0, M24CXX_READ_PAGE_SIZE);

        if (m24cxx_read(dev, addr, rx_buf, M24CXX_READ_PAGE_SIZE) != M24CXX_OK) {
            printf("\n[ERROR] m24cxx_read failed at 0x%05lX\n", addr);
            return;
        }

        read_crc = calculate_crc32(rx_buf, M24CXX_READ_PAGE_SIZE, read_crc);

        bytes_read += M24CXX_READ_PAGE_SIZE;

        // Print a dot for every 1 kB (1024 bytes)
        if (bytes_read % 1024 == 0) {
            printf(".");
            fflush(stdout);

            // Wrap line and show progress every 16 kB
            if ((bytes_read / 1024) % 16 == 0) {
                printf(" [%lu kB / %lu ms]\n", bytes_read / 1024, systick - start_read_tick);
            }
        }
    }

    if ((bytes_read / 1024) % 16 != 0) {
        printf("\n");
    }

    uint32_t read_elapsed_ms = systick - start_read_tick;
    printf("Read Complete in %lu ms | CRC32: 0x%08lX\n\n", read_elapsed_ms, read_crc);

    // --- VERIFICATION ---
    if (write_crc == read_crc) {
        printf("SUCCESS: CRC Match (0x%08lX)\n\n", write_crc);
    } else {
        printf("FAILURE: CRC Mismatch! Write: 0x%08lX != Read: 0x%08lX\n\n", write_crc, read_crc);
    }
}

/**
 * @brief Main entry point for Core 0.
 */
int main() {

    // Increase voltage to 1.3V for stable operation at 320 MHz (default is 1.2V)
    vreg_set_voltage(VREG_VOLTAGE_1_35);

    // Bump mcu clock to 320 MHz for maximum performance (default is 125 MHz)
    set_sys_clock_khz(320000, true);

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
        if (reserved_addr(addr))
            ret = PICO_ERROR_GENERIC;
        else
            ret = i2c_read_blocking(i2c_default, addr, &rxdata, 1, false);

        printf(ret < 0 ? "." : "*");
        printf(addr % 16 == 15 ? "\n" : "  ");
    }
    printf("Done.\n");

    // Initialize m24cxx library instance
    m24cxx_t m24cxx_dev;
    m24cxx_init(&m24cxx_dev, i2c_default, 0x50, -1); // No WP pin used in this example

    // Benchmark test size: Change to 16 * 1024 for quick 16 kB test, or M24CXX_SIZE for full 512 kB benchmark
    run_m24cxx_demo(&m24cxx_dev, M24CXX_SIZE);
    // run_m24cxx_demo(&m24cxx_dev, 16 * 1024);

    uint32_t erase_start_tick = systick;
    printf("\nErasing entire %s memory (%lu kB)...\n", M24CXX_TYPE, M24CXX_SIZE / 1024);
    m24cxx_erase(&m24cxx_dev, 0, M24CXX_SIZE);
    uint32_t erase_elapsed_ms = systick - erase_start_tick;
    printf("Erase Complete in %lu ms\n", erase_elapsed_ms);

    // Reset systick to 0 to avoid overflow issues in the main loop
    systick = 0;

    uint32_t now, loop_cnt = 0, next_blink = blink_interval_ms, next_tick = TICK_DELAY;

    while (true) {

        now = systick;

        if (now >= next_blink) {
            pico_toggle_led();
            next_blink = now + blink_interval_ms;
        }

        if (now >= next_tick) {
            printf("Core 0 tick %lu (loop = %lu)\n", now / 1000, loop_cnt);
            loop_cnt = 0;
            next_tick = now + TICK_DELAY;
        }

        ++loop_cnt;
    }
}

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

// vim: ts=4 et nowrap