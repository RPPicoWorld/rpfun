/**
 * @file main.c
 * @brief Maintaining a LittleFS on RP2350 flash
 * @author STM32World <lth@stm32world.com>
 * @date 2026
 *
 * Copyright (c) 2026 STM32World <lth@stm32world.com>
 *
 * Maintains a LittleFS filesystem on the RP2350 flash memory. The program mounts the LittleFS,
 * lists the root directory and dumps the contents of a file. It then keeps a boot counter and a
 * total uptime counter in files, and times writing and reading a 100 KB file.
 * It also toggles the onboard LED at regular intervals and prints tick information to the console.
 *
 * The system clock and core voltage come from CMake (stock 150 MHz / 1.10 V by default), and the
 * filesystem also works when overclocked, e.g. at 540 MHz / 1.60 V (see init_clock() here and the
 * flash erase/program wrappers in lfs_port.c).
 */

// Include necessary headers from the Pico SDK
#include "hardware/clocks.h"      // For clock frequency information
#include "hardware/dma.h"         // For DMA operations
#include "hardware/flash.h"       // For flash timing / cache helpers
#include "hardware/gpio.h"        // For GPIO control (LED)
#include "hardware/timer.h"       // For timer operations
#include "hardware/vreg.h"        // For voltage regulator control
#include "pico/bootrom.h"         // For rom_flash_enter_cmd_xip
#include "pico/multicore.h"       // For multicore operations
#include "pico/rand.h"            // For random number generation
#include "pico/stdlib.h"          // For standard I/O and sleep functions

// Include Standard C headers
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Include LittleFS headers
#include "lfs_port.h"

// Clock and core voltage, normally set from CMake (-DSYS_CLOCK_KHZ=... -DCORE_VOLTAGE=...)
#ifndef SYS_CLOCK_KHZ
#define SYS_CLOCK_KHZ 150000
#endif

#ifndef CORE_VOLTAGE
#define CORE_VOLTAGE VREG_VOLTAGE_1_10
#endif

// Define constants for LED and tick delays
#define LED_DELAY 500
#define TICK_DELAY 1000

// Define file names for boot count, uptime, and performance testing
const char *bootcnt_file = "bootcnt.dat"; // File to store boot count
const char *uptime_file = "uptime.dat";   // File to store total uptime
const char *big_file = "big.dat";         // File to test read/write performance

// The mounted LittleFS instance
static lfs_t lfs;

// Volatile variable to mimic STM32's uwTick
static volatile uint32_t systick = 0;

// Total uptime in seconds over all boots, counted in the main loop and saved to uptime.dat every 10 seconds
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

    lfs_file_rewind(&lfs, &file); // Back to the start, so the new count overwrites the old one
    lfs_file_write(&lfs, &file, &boot_count, sizeof(boot_count));
    lfs_file_close(&lfs, &file);
}

/**
 * @brief Loads the total uptime from LittleFS.
 *
 * Reads the 32-bit uptime (seconds) from 'uptime.dat' into total_uptime and prints it.
 * The file is created if it doesn't exist, in which case the uptime starts at 0.
 */
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

/**
 * @brief Saves the current total uptime to LittleFS.
 *
 * Overwrites the 32-bit value in 'uptime.dat' with total_uptime.
 */
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
 *
 * Writes 400 blocks of 256 bytes to 'big.dat' (truncating it first), then reads the file back
 * in the same blocks, and prints how long each took in milliseconds.
 */
void flash_performance(void) {

    lfs_file_t file;
    int err;
    uint32_t start_time;
    uint8_t buffer[256];

    // Fill the buffer with a simple counting pattern
    for (int i = 0; i < sizeof(buffer); ++i) {
        buffer[i] = (uint8_t)i;
    }

    err = lfs_file_open(&lfs, &file, big_file, LFS_O_RDWR | LFS_O_CREAT | LFS_O_TRUNC);
    if (err < 0) {
        printf("Failed to open %s (err %d)\n", big_file, err);
        return;
    }

    // Write 400 x 256 bytes = 100 KB; LittleFS erases and programs flash as needed
    printf("Writing 100KB to %s...\n", big_file);
    start_time = systick;
    for (int i = 0; i < 4 * 100; ++i) {
        lfs_file_write(&lfs, &file, buffer, sizeof(buffer));
    }
    lfs_file_close(&lfs, &file); // Closing commits the file, so it is included in the timing

    printf("Write completed in %lu ms\n", (unsigned long)(systick - start_time));

    err = lfs_file_open(&lfs, &file, big_file, LFS_O_RDONLY);
    if (err < 0) {
        printf("Failed to open %s for reading (err %d)\n", big_file, err);
        return;
    }

    // Read the 100 KB back; reads come straight from the memory-mapped flash
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
 * @brief Prints the contents of a text file in the LittleFS.
 * @param lfs_ptr Pointer to the LittleFS instance.
 * @param path The path of the file to print.
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
    // Read one chunk at a time, leaving room for the terminating zero printf needs
    while ((read_bytes = lfs_file_read(lfs_ptr, &file, buf, sizeof(buf) - 1)) > 0) {
        buf[read_bytes] = '\0';
        printf("%s", buf);
    }
    printf("\n--- End of '%s' ---\n", path);

    lfs_file_close(lfs_ptr, &file);
}

/**
 * @brief Sets the core voltage and system clock (stock 150 MHz / 1.10 V unless overridden from CMake).
 *
 * Above ~320 MHz the flash can't keep up with the fast quad XIP setup from boot, so switch to the
 * slow but safe serial (03h) read mode first, which the ROM runs at clk_sys / 12. lfs_port.c keeps
 * this setup in place across flash erase/program.
 */
static void init_clock(void) {
    vreg_disable_voltage_limit();   // Allow voltages above 1.30 V
    vreg_set_voltage(CORE_VOLTAGE); // Core voltage, set from CMake
    sleep_ms(10);                   // Give the regulator time to settle before raising the clock

#if SYS_CLOCK_KHZ > 320000
    rom_flash_enter_cmd_xip();
#endif

    set_sys_clock_khz(SYS_CLOCK_KHZ, true); // System clock, set from CMake, true means to wait for the clock to stabilize
}

/**
 * @brief Main function.
 *
 * Sets up the clock, LED and UART, mounts the filesystem and runs the file tests, then blinks
 * the LED and prints a tick once a second, saving the total uptime every 10 seconds.
 *
 * @return int Never returns.
 */
int main() {

    init_clock(); // Stock 150 MHz unless overridden from CMake

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

    // Mount the LittleFS partition at 0x10100000 (offset 1 MB in flash, see linker_overrides/memory_flash.incl)
    if (lfs_port_init(&lfs) == 0) {
        printf("LittleFS mounted successfully!\n");
        list_lfs_directory(&lfs, "/");
        dump_lfs_file(&lfs, "dummy.txt");
    } else {
        // Nothing useful to do without the filesystem (has littlefs.bin been flashed?)
        panic("Fatal Error: Failed to mount LittleFS!\n");
    }

    // The filesystem is mounted, so count this boot
    update_bootcount();

    // Initialize total uptime from the filesystem
    init_total_uptime();

    // Measure the performance of writing and reading a 100KB file on LittleFS
    flash_performance();

    // loop_cnt counts main loop iterations per second, a rough measure of how fast the core runs
    uint32_t now, loop_cnt = 0, next_blink = LED_DELAY, next_tick = TICK_DELAY;

    // Main loop for Core 0
    while (true) {

        now = systick;

        // Toggle the LED every LED_DELAY ms
        if (now >= next_blink) {
            pico_toggle_led();
            next_blink += LED_DELAY;
        }

        // Once a second: count the uptime and print a tick
        if (now >= next_tick) {

            ++total_uptime;

            printf("RP tick %lu (loop = %lu total = %lu)\n", now / 1000, loop_cnt, total_uptime);

            // Save the uptime every 10 seconds to limit flash wear (up to 10 s is lost on reset)
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
