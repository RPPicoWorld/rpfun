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
#include <string.h>

#include "lfs_port.h"

#define LED_DELAY 500   // LED blink delay in milliseconds
#define TICK_DELAY 1000 // Tick delay in milliseconds

static lfs_t lfs;

// Volatile variable to mimic STM32's uwTick
static volatile uint32_t systick = 0;

/**
 * @brief List all directory entries in root to diagnose available files.
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
 * @brief Dump the contents of a file from LittleFS to stdout.
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
 * @brief Reads, increments, and updates the boot count in 'bootcount.dat'.
 */
static void update_boot_count(lfs_t *lfs_ptr) {
    lfs_file_t file;
    uint32_t boot_count = 0;

    // Read current boot count if file exists
    int err = lfs_file_open(lfs_ptr, &file, "bootcount.dat", LFS_O_RDONLY);
    if (err == LFS_ERR_OK) {
        lfs_file_read(lfs_ptr, &file, &boot_count, sizeof(boot_count));
        lfs_file_close(lfs_ptr, &file);
    }

    boot_count++;

    // Write back updated boot count
    err = lfs_file_open(lfs_ptr, &file, "bootcount.dat", LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC);
    if (err == LFS_ERR_OK) {
        lfs_file_write(lfs_ptr, &file, &boot_count, sizeof(boot_count));
        lfs_file_close(lfs_ptr, &file);
        printf("Boot count: %lu\n", (unsigned long)boot_count);
    } else {
        printf("Failed to update 'bootcount.dat' (err %d)\n", err);
    }
}

/**
 * @brief Performs a 512 kB write and read speed test using 'big.dat'.
 * Overwrites 'big.dat' if it already exists, timed via the systick millisecond counter.
 */
static void run_speed_test(lfs_t *lfs_ptr) {
    lfs_file_t file;
    static uint8_t chunk_buf[4096];       // 4 kB buffer matching LittleFS block size
    const size_t total_size = 256 * 1024; // 512 kB
    const size_t chunk_size = sizeof(chunk_buf);
    const size_t chunks = total_size / chunk_size;

    memset(chunk_buf, 0xAA, chunk_size);

    printf("--- LittleFS Speed Test (256 kB) ---\n");

    // 1. Write Benchmark (LFS_O_CREAT | LFS_O_TRUNC forces overwrite if big.dat exists)
    uint32_t start_ms = systick;
    int err = lfs_file_open(lfs_ptr, &file, "big.dat", LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC);
    if (err < 0) {
        printf("Failed to open 'big.dat' for writing (err %d)\n", err);
        return;
    }

    for (size_t i = 0; i < chunks; i++) {
        lfs_ssize_t written = lfs_file_write(lfs_ptr, &file, chunk_buf, chunk_size);
        if (written < 0) {
            printf("Write failed on chunk %zu (err %ld)\n", i, (long)written);
            lfs_file_close(lfs_ptr, &file);
            return;
        }
    }
    lfs_file_close(lfs_ptr, &file);
    uint32_t write_time_ms = systick - start_ms;

    double write_sec = (double)write_time_ms / 1000.0;
    double write_kbps = (write_sec > 0.0) ? ((double)(total_size / 1024) / write_sec) : 0.0;
    printf("Write: %zu kB written in %lu ms (%.2f kB/s)\n", total_size / 1024, (unsigned long)write_time_ms, write_kbps);

    // 2. Read Benchmark
    start_ms = systick;
    err = lfs_file_open(lfs_ptr, &file, "big.dat", LFS_O_RDONLY);
    if (err < 0) {
        printf("Failed to open 'big.dat' for reading (err %d)\n", err);
        return;
    }

    size_t total_read = 0;
    lfs_ssize_t read_bytes;
    while ((read_bytes = lfs_file_read(lfs_ptr, &file, chunk_buf, chunk_size)) > 0) {
        total_read += read_bytes;
    }
    lfs_file_close(lfs_ptr, &file);
    uint32_t read_time_ms = systick - start_ms;

    double read_sec = (double)read_time_ms / 1000.0;
    double read_kbps = (read_sec > 0.0) ? ((double)(total_read / 1024) / read_sec) : 0.0;
    printf("Read:  %zu kB read in %lu ms (%.2f kB/s)\n", total_read / 1024, (unsigned long)read_time_ms, read_kbps);
    printf("--- End of Speed Test ---\n");
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

    // Start the heartbeat timer early so systick is active during filesystem tests
    universal_tick_init();

    // Mount LittleFS partition at offset 0x10100000
    if (lfs_port_init(&lfs) == 0) {
        printf("LittleFS mounted successfully!\n");
        update_boot_count(&lfs);
        list_lfs_directory(&lfs, "/");
        dump_lfs_file(&lfs, "dummy.txt");
        run_speed_test(&lfs);
    } else {
        printf("Failed to mount LittleFS!\n");
    }

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