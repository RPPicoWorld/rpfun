/**
 * @file main.c
 * @brief Maintaining a LittleFS on RP2350 flash
 * @author STM32World <lth@stm32world.com>
 * @date 2026
 *
 * Copyright (c) 2026 STM32World <lth@stm32world.com>
 *
 * Maintains a LittleFS filesystem on the RP2350 flash memory. The program mounts the LittleFS,
 * lists the root directory and dumps the contents of a file. It then keeps a boot counter in a
 * file and times writing and reading a 400 KB file.
 *
 * After that both cores run: each prints a tick once a second (core 1 offset by 500 ms from
 * core 0) and keeps its own total uptime file (uptime0.dat and uptime1.dat), saved every 10
 * seconds, so the filesystem is written from both cores. Core 0 also toggles the onboard LED.
 *
 * The system clock and core voltage come from CMake (stock 150 MHz / 1.10 V by default), and the
 * filesystem also works when overclocked, e.g. at 540 MHz / 1.60 V (see init_clock() here and the
 * flash erase/program wrappers in lfs_port.c).
 */

// Include necessary headers from the Pico SDK
#include "hardware/clocks.h"      // For clock frequency information
#include "hardware/dma.h"         // For DMA operations
#include "hardware/flash.h"       // For flash timing / cache helpers
#include "hardware/structs/qmi.h" // QMI (flash interface) registers, for the overclocked quad XIP setup
#include "hardware/sync.h"        // save_and_disable_interrupts()
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

// Size of the file written and read back by flash_performance()
#define BIG_FILE_KB 400

#define UPTIME_SAVE_INTERVAL 10 // Save each core's uptime every 10 seconds

// Define file names for boot count, uptime, and performance testing
const char *bootcnt_file = "bootcnt.dat";                         // File to store boot count
const char *uptime_files[NUM_CORES] = {"uptime0.dat", "uptime1.dat"}; // Total uptime of core 0 and core 1
const char *big_file = "big.dat";                                 // File to test read/write performance

// The mounted LittleFS instance
static lfs_t lfs;

// Volatile variable to mimic STM32's uwTick (incremented by the timer callback, read by both cores)
static volatile uint32_t systick = 0;

// Total uptime in seconds over all boots, one per core. Each core only touches its own entry:
// it counts it up in its main loop and saves it to its own file every UPTIME_SAVE_INTERVAL seconds.
static uint32_t core_uptime[NUM_CORES];

// Mutex for synchronizing access to printf, so lines from the two cores don't get mixed up
auto_init_mutex(printf_mutex);

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
 * @brief Loads this core's total uptime from LittleFS.
 *
 * Reads the 32-bit uptime (seconds) from this core's file (uptime0.dat or uptime1.dat) into
 * core_uptime[] and prints it. The file is created if it doesn't exist, in which case the uptime
 * starts at 0.
 */
static void load_core_uptime(void) {
    uint core = get_core_num();
    const char *path = uptime_files[core];
    uint32_t uptime = 0;
    lfs_file_t file;

    int err = lfs_file_open(&lfs, &file, path, LFS_O_RDWR | LFS_O_CREAT);
    if (err < 0) {
        mutex_enter_blocking(&printf_mutex);
        printf("Core %u: failed to open %s (err %d)\n", core, path, err);
        mutex_exit(&printf_mutex);
        return;
    }

    lfs_ssize_t read_bytes = lfs_file_read(&lfs, &file, &uptime, sizeof(uptime));
    if (read_bytes != sizeof(uptime)) {
        uptime = 0; // New (empty) file
    }
    lfs_file_close(&lfs, &file);

    core_uptime[core] = uptime;

    mutex_enter_blocking(&printf_mutex);
    printf("Core %u: loaded %s, total uptime %lu seconds\n", core, path, (unsigned long)uptime);
    mutex_exit(&printf_mutex);
}

/**
 * @brief Saves this core's total uptime to LittleFS and prints what was saved.
 *
 * Overwrites the 32-bit value in this core's file (uptime0.dat or uptime1.dat) with its entry in
 * core_uptime[], and prints the value and how long the save took (including any wait for the
 * other core to finish its own filesystem access).
 */
static void save_core_uptime(void) {
    uint core = get_core_num();
    const char *path = uptime_files[core];
    uint32_t uptime = core_uptime[core];
    uint32_t start_us = time_us_32();
    lfs_file_t file;

    int err = lfs_file_open(&lfs, &file, path, LFS_O_RDWR | LFS_O_CREAT);
    if (err >= 0) {
        lfs_ssize_t written = lfs_file_write(&lfs, &file, &uptime, sizeof(uptime));
        err = lfs_file_close(&lfs, &file); // The data is committed to flash on close
        if (written != sizeof(uptime)) {
            err = (written < 0) ? (int)written : LFS_ERR_IO;
        }
    }
    uint32_t elapsed_us = time_us_32() - start_us;

    mutex_enter_blocking(&printf_mutex);
    if (err < 0) {
        printf("Core %u: failed to save %s (err %d)\n", core, path, err);
    } else {
        printf("Core %u: saved %s, total uptime %lu seconds (%lu.%03lu ms)\n", core, path, (unsigned long)uptime,
               (unsigned long)(elapsed_us / 1000), (unsigned long)(elapsed_us % 1000));
    }
    mutex_exit(&printf_mutex);
}

/**
 * @brief Measures the performance of writing and reading a BIG_FILE_KB file on LittleFS.
 *
 * Writes BIG_FILE_KB * 4 blocks of 256 bytes to 'big.dat' (truncating it first), then reads the file back
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

    // Write BIG_FILE_KB * 4 x 256 bytes; LittleFS erases and programs flash as needed
    printf("Writing %dKB to %s...\n", BIG_FILE_KB, big_file);
    start_time = systick;
    for (int i = 0; i < BIG_FILE_KB * 4; ++i) {
        lfs_file_write(&lfs, &file, buffer, sizeof(buffer));
    }
    lfs_file_close(&lfs, &file); // Closing commits the file, so it is included in the timing

    printf("Write completed in %lu ms\n", (unsigned long)(systick - start_time));

    err = lfs_file_open(&lfs, &file, big_file, LFS_O_RDONLY);
    if (err < 0) {
        printf("Failed to open %s for reading (err %d)\n", big_file, err);
        return;
    }

    // Read the file back; reads come straight from the memory-mapped flash
    printf("Reading %dKB from %s...\n", BIG_FILE_KB, big_file);
    start_time = systick;
    for (int i = 0; i < BIG_FILE_KB * 4; ++i) {
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

#if SYS_CLOCK_KHZ > 320000
// Quad XIP setup used above 320 MHz: clk_sys / 6, i.e. 90 MHz flash clock at 540 MHz.
// Measured at 540 MHz on the Streamline board: at this divider RX delays 1 to 7 all read the
// flash correctly (0 doesn't), so use 4, in the middle of the working range.
#define XIP_CLKDIV 6
#define XIP_RXDELAY 4

/**
 * @brief Switches XIP back to the boot time quad read, with a clock divider suited to the higher clock.
 *
 * Runs from RAM, as the flash can't be read while its read setup changes. The boot setup sends the
 * read command on every access (no continuous read mode), so it can be switched to at any time.
 *
 * @param timing Boot time QMI window 0 timing register (clock divider and RX delay get replaced).
 * @param rfmt Boot time read format register (quad 0xeb read).
 * @param rcmd Boot time read command register.
 */
static void __no_inline_not_in_flash_func(enter_quad_xip)(uint32_t timing, uint32_t rfmt, uint32_t rcmd) {
    timing &= ~(QMI_M0_TIMING_CLKDIV_BITS | QMI_M0_TIMING_RXDELAY_BITS);
    timing |= (XIP_CLKDIV << QMI_M0_TIMING_CLKDIV_LSB) | (XIP_RXDELAY << QMI_M0_TIMING_RXDELAY_LSB);

    uint32_t ints = save_and_disable_interrupts();
    qmi_hw->m[0].timing = timing;
    qmi_hw->m[0].rfmt = rfmt;
    qmi_hw->m[0].rcmd = rcmd;
    flash_flush_cache(); // Drop anything cached with the old setup
    restore_interrupts(ints);
}
#endif

/**
 * @brief Sets the core voltage and system clock (stock 150 MHz / 1.10 V unless overridden from CMake).
 *
 * Above ~320 MHz the flash can't keep up with the fast quad XIP setup from boot (clock divider 3),
 * so switch to the slow but safe serial (03h) read mode while raising the clock, then back to quad
 * read with a bigger clock divider (see enter_quad_xip()). lfs_port.c keeps whatever setup is in
 * use across flash erase/program.
 */
static void init_clock(void) {
#if SYS_CLOCK_KHZ > 320000
    // Remember the boot time quad read setup before switching to serial mode
    uint32_t boot_timing = qmi_hw->m[0].timing;
    uint32_t boot_rfmt = qmi_hw->m[0].rfmt;
    uint32_t boot_rcmd = qmi_hw->m[0].rcmd;
#endif

    vreg_disable_voltage_limit();   // Allow voltages above 1.30 V
    vreg_set_voltage(CORE_VOLTAGE); // Core voltage, set from CMake
    sleep_ms(10);                   // Give the regulator time to settle before raising the clock

#if SYS_CLOCK_KHZ > 320000
    rom_flash_enter_cmd_xip(); // Serial read at clk_sys / 12, safe at any clock
#endif

    set_sys_clock_khz(SYS_CLOCK_KHZ, true); // System clock, set from CMake, true means to wait for the clock to stabilize

#if SYS_CLOCK_KHZ > 320000
    enter_quad_xip(boot_timing, boot_rfmt, boot_rcmd); // Back to quad read, now at clk_sys / XIP_CLKDIV
#endif
}

// systick value both cores count their ticks from, set by core 0 just before launching core 1
static volatile uint32_t ticks_start = 0;

/**
 * @brief Runs one core's tick loop: a tick every TICK_DELAY ms, uptime saved every 10 seconds.
 *
 * Both cores run this. Core 1's ticks are offset by TICK_DELAY / 2 (500 ms) from core 0's, so
 * the two cores print, and save their uptime files, at different times. Core 0 also blinks the
 * LED. Each core counts its own uptime and saves it to its own file (see save_core_uptime()).
 */
static void __attribute__((noreturn)) tick_loop(void) {
    uint core = get_core_num();

    // loop_cnt counts main loop iterations per second, a rough measure of how fast the core runs
    uint32_t now, loop_cnt = 0, seconds = 0;
    uint32_t next_tick = ticks_start + TICK_DELAY + (core == 1 ? TICK_DELAY / 2 : 0);
    uint32_t next_blink = ticks_start + LED_DELAY;

    while (true) {

        now = systick;

        // Core 0 toggles the LED every LED_DELAY ms
        if (core == 0 && now >= next_blink) {
            pico_toggle_led();
            next_blink += LED_DELAY;
        }

        // Once a second: count the uptime and print a tick
        if (now >= next_tick) {

            ++core_uptime[core];
            ++seconds;

            mutex_enter_blocking(&printf_mutex);
            printf("Core %u tick %lu ms (loop = %lu total = %lu)\n", core, (unsigned long)now, (unsigned long)loop_cnt,
                   (unsigned long)core_uptime[core]);
            mutex_exit(&printf_mutex);

            // Save the uptime every UPTIME_SAVE_INTERVAL seconds to limit flash wear (up to that much is lost on reset)
            if (seconds % UPTIME_SAVE_INTERVAL == 0) {
                save_core_uptime();
            }

            loop_cnt = 0;
            next_tick += TICK_DELAY;
        }

        ++loop_cnt;

        tight_loop_contents(); // Allow other tasks to run and prevent CPU hogging
    }
}

/**
 * @brief Entry point for Core 1.
 *
 * Makes core 1 pausable while core 0 writes to flash, loads core 1's uptime and runs the tick loop.
 */
static void core1_entry(void) {
    multicore_lockout_victim_init(); // Let core 0 pause this core while it erases/programs flash

    mutex_enter_blocking(&printf_mutex);
    printf("Core 1: Booting...\n");
    mutex_exit(&printf_mutex);

    load_core_uptime();

    tick_loop();
}

/**
 * @brief Main function.
 *
 * Sets up the clock, LED and UART, mounts the filesystem and runs the file tests on core 0 alone,
 * then launches core 1 and runs the tick loop on both cores.
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

    // Measure the performance of writing and reading a BIG_FILE_KB file on LittleFS
    flash_performance();

    // Load core 0's total uptime from the filesystem
    load_core_uptime();

    // From here on both cores use the filesystem. Let core 1 pause this core while it writes to flash.
    multicore_lockout_victim_init();

    // Launch core 1 and wait until it can be paused, before core 0 writes to flash again
    ticks_start = systick;
    multicore_launch_core1(core1_entry);
    while (!multicore_lockout_victim_is_initialized(1)) {
        tight_loop_contents();
    }

    tick_loop(); // Never returns
}

// vim: ts=4 et nowrap
