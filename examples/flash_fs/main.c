/**
 * @file main.c
 * @brief Maintaining a LittleFS on RP2350 flash
 * @author STM32World <lth@stm32world.com>
 * @date 2026
 *
 * Copyright (c) 2026 STM32World <lth@stm32world.com>
 *
 * Maintains a LittleFS filesystem on the RP2350 flash memory. The program initializes the LittleFS, mounts it,
 * lists the root directory, and dumps the contents of a specified file.
 * It also toggles the onboard LED at regular intervals and prints tick information to the console.
 *
 */

// Include necessary headers from the Pico SDK
#include "hardware/clocks.h" // For clock frequency information
#include "hardware/dma.h"    // For DMA operations
#include "hardware/gpio.h"   // For GPIO control (LED)
#include "hardware/timer.h"  // For timer operations
#include "hardware/vreg.h"   // For voltage regulator control
#include "pico/multicore.h"  // For multicore operations
#include "pico/rand.h"       // For random number generation
#include "pico/stdlib.h"     // For standard I/O and sleep functions

// Include Standard C headers
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Include LittleFS headers
#include "lfs_port.h"

// Define constants for LED and tick delays
#define LED_DELAY 500
#define TICK_DELAY 1000

// Define file names for boot count, uptime, and performance testing
const char *bootcnt_file = "bootcnt.dat"; // File to store boot count
const char *uptime_file = "uptime.dat";   // File to store total uptime
const char *big_file = "big.dat";         // File to test read/write performance

// Declare a static instance of the LittleFS structure and a volatile variable for system ticks
static lfs_t lfs;

// Volatile variable to mimic STM32's uwTick
static volatile uint32_t systick = 0;

// Total uptime in seconds, incremented every 1000ms by the timer callback
static volatile uint32_t total_uptime = 0;

/**
 * @brief Timer callback function to increment the systick counter.
 *
 * @param t Pointer to the repeating timer structure.
 * @return bool True to continue the timer, false to stop it.
 */
bool on_timer_tick(struct repeating_timer *t) {
    systick++;
    return true;
}

/**
 * @brief Initializes a repeating timer to increment the systick counter.
 */
void universal_tick_init() {
    static struct repeating_timer timer;
    // Negative delay means "measure from the start of the last callback"
    // to avoid jitter. -1ms = 1000us frequency.
    add_repeating_timer_ms(-1, on_timer_tick, NULL, &timer);
}

/**
 * @brief Initializes the onboard LED for output.
 */
int pico_led_init(void) {
    gpio_init(PICO_DEFAULT_LED_PIN);              // The LED pin is defined in the board header as PICO_DEFAULT_LED_PIN
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT); // Set the LED pin as an output
    return PICO_OK;
}

/**
 * @brief Toggles the state of the onboard LED.
 */
void pico_toggle_led() {
    gpio_xor_mask64(((uint64_t)1 << PICO_DEFAULT_LED_PIN));
}

/**
 * @brief Updates the boot count stored in LittleFS.
 *
 * Reads the 32-bit boot count from 'bootcnt.dat', prints the current value,
 * increments it by 1, and writes it back to the filesystem.
 */
static void update_bootcount(void) {
    lfs_file_t file;
    uint32_t boot_count = 0;

    int err = lfs_file_open(&lfs, &file, bootcnt_file, LFS_O_RDWR | LFS_O_CREAT);
    if (err < 0) {
        printf("Failed to open bootcnt.dat (err %d)\n", err);
        return;
    }

    lfs_ssize_t read_bytes = lfs_file_read(&lfs, &file, &boot_count, sizeof(boot_count));
    if (read_bytes != sizeof(boot_count)) {
        boot_count = 0;
    }

    printf("Boot count: %lu\n", (unsigned long)boot_count);

    boot_count++;

    lfs_file_rewind(&lfs, &file);
    lfs_file_write(&lfs, &file, &boot_count, sizeof(boot_count));
    lfs_file_close(&lfs, &file);
}

void init_total_uptime(void) {
    lfs_file_t file;

    int err = lfs_file_open(&lfs, &file, uptime_file, LFS_O_RDWR | LFS_O_CREAT);
    if (err < 0) {
        printf("Failed to open uptime.dat (err %d)\n", err);
        return;
    }

    lfs_ssize_t read_bytes = lfs_file_read(&lfs, &file, &total_uptime, sizeof(total_uptime));
    if (read_bytes != sizeof(total_uptime)) {
        total_uptime = 0;
    }

    printf("Total uptime: %lu seconds\n", (unsigned long)total_uptime);

    lfs_file_close(&lfs, &file);
}

void update_total_uptime(void) {
    lfs_file_t file;

    int err = lfs_file_open(&lfs, &file, uptime_file, LFS_O_RDWR | LFS_O_CREAT);
    if (err < 0) {
        printf("Failed to open %s (err %d)\n", uptime_file, err);
        return;
    }

    lfs_file_write(&lfs, &file, &total_uptime, sizeof(total_uptime));
    lfs_file_close(&lfs, &file);
}

/**
 * @brief Measures the performance of writing and reading a 100KB file on LittleFS.
 */
void flash_performance(void) {

    lfs_file_t file;
    int err;
    uint32_t start_time;
    uint8_t buffer[256];

    for (int i = 0; i < sizeof(buffer); ++i) {
        buffer[i] = (uint8_t)i;
    }

    err = lfs_file_open(&lfs, &file, big_file, LFS_O_RDWR | LFS_O_CREAT | LFS_O_TRUNC);
    if (err < 0) {
        printf("Failed to open %s (err %d)\n", big_file, err);
        return;
    }

    printf("Writing 100KB to %s...\n", big_file);
    start_time = systick;
    for (int i = 0; i < 4 * 100; ++i) {
        lfs_file_write(&lfs, &file, buffer, sizeof(buffer));
    }
    lfs_file_close(&lfs, &file);

    printf("Write completed in %lu ms\n", (unsigned long)(systick - start_time));

    err = lfs_file_open(&lfs, &file, big_file, LFS_O_RDONLY);
    if (err < 0) {
        printf("Failed to open %s for reading (err %d)\n", big_file, err);
        return;
    }

    printf("Reading 100KB from %s...\n", big_file);
    start_time = systick;
    for (int i = 0; i < 4 * 100; ++i) {
        lfs_file_read(&lfs, &file, buffer, sizeof(buffer));
    }
    lfs_file_close(&lfs, &file);
    printf("Read completed in %lu ms\n", (unsigned long)(systick - start_time));
}

/**
 * @brief Lists the contents of a directory in the LittleFS.
 * @param lfs_ptr Pointer to the LittleFS instance.
 * @param path The path of the directory to list.
 */
static void list_lfs_directory(lfs_t *lfs_ptr, const char *path) {
    lfs_dir_t dir;
    struct lfs_info info;

    int err = lfs_dir_open(lfs_ptr, &dir, path);
    if (err < 0) {
        printf("Failed to open directory '%s' (err %d)\n", path, err);
        return;
    }

    printf("--- Directory listing for '%s' ---\n", path);
    while (lfs_dir_read(lfs_ptr, &dir, &info) > 0) {
        printf("  %s (%s, %lu bytes)\n", info.name, (info.type == LFS_TYPE_REG) ? "FILE" : "DIR", (unsigned long)info.size);
    }
    printf("--- End of listing ---\n");

    lfs_dir_close(lfs_ptr, &dir);
}

/**
 * @brief Initializes the LittleFS on the RP2350 flash.
 * @param lfs Pointer to the LittleFS instance to initialize.
 * @return int Returns 0 on success, or a negative error code on failure.
 */
static void dump_lfs_file(lfs_t *lfs_ptr, const char *path) {
    lfs_file_t file;
    int err = lfs_file_open(lfs_ptr, &file, path, LFS_O_RDONLY);
    if (err < 0) {
        printf("Failed to open '%s' (err %d)\n", path, err);
        return;
    }

    printf("--- Dumping '%s' ---\n", path);
    char buf[128];
    lfs_ssize_t read_bytes;
    while ((read_bytes = lfs_file_read(lfs_ptr, &file, buf, sizeof(buf) - 1)) > 0) {
        buf[read_bytes] = '\0';
        printf("%s", buf);
    }
    printf("\n--- End of '%s' ---\n", path);

    lfs_file_close(lfs_ptr, &file);
}

/**
 * @brief Main function.
 * @return int Return code.
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

    // Start the heartbeat timer early so systick is active during filesystem tests
    universal_tick_init();

    // Mount LittleFS partition at offset 0x10100000
    if (lfs_port_init(&lfs) == 0) {
        printf("LittleFS mounted successfully!\n");
        list_lfs_directory(&lfs, "/");
        dump_lfs_file(&lfs, "dummy.txt");
    } else {
        // printf("Failed to mount LittleFS!\n");
        panic("Fatal Error: Failed to mount LittleFS!\n");
    }

    // If we get here, we have successfully mounted the filesystem and performed our operations.
    update_bootcount();

    // Initialize total uptime from the filesystem
    init_total_uptime();

    // Measure the performance of writing and reading a 100KB file on LittleFS
    flash_performance();

    uint32_t now, loop_cnt = 0, next_blink = LED_DELAY, next_tick = TICK_DELAY;

    // Main loop for Core 0
    while (true) {

        now = systick;

        if (now >= next_blink) {
            pico_toggle_led();
            next_blink += LED_DELAY;
        }

        if (now >= next_tick) {

            ++total_uptime;

            printf("RP tick %lu (loop = %lu total = %lu)\n", now / 1000, loop_cnt, total_uptime);

            if (next_tick % 10000 == 0) {
                update_total_uptime();
            }

            loop_cnt = 0;
            next_tick += TICK_DELAY;
        }

        ++loop_cnt;

        tight_loop_contents(); // Allow other tasks to run and prevent CPU hogging
    }
}

// vim: ts=4 et nowrap
