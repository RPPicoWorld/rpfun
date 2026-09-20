#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "hardware/vreg.h"
#include "pico/multicore.h"
#include "pico/rand.h"
#include "pico/stdlib.h"

#include <stdint.h>
#include <stdio.h>

#include "lfs_port.h"

#define LED_DELAY 500
#define TICK_DELAY 1000

static lfs_t lfs;
static volatile uint32_t systick = 0;

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

bool on_timer_tick(struct repeating_timer *t) {
    systick++;
    return true;
}

void universal_tick_init() {
    static struct repeating_timer timer;
    add_repeating_timer_ms(-1, on_timer_tick, NULL, &timer);
}

int pico_led_init(void) {
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    return PICO_OK;
}

void pico_toggle_led() {
    gpio_xor_mask64(((uint64_t)1 << PICO_DEFAULT_LED_PIN));
}

int main() {
    int rc = pico_led_init();
    hard_assert(rc == PICO_OK);

    stdio_init_all();
    uart_set_baudrate(uart0, 921600);
    sleep_ms(50);

    printf("\n\n\nCore 0: Booting...\n");
    printf("Running on %s at %d MHz\n",
#ifdef __riscv
           "RISC-V",
#else
           "Arm Cortex-M33",
#endif
           frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_SYS) / 1000);

    if (lfs_port_init(&lfs) == 0) {
        printf("LittleFS mounted successfully!\n");
        list_lfs_directory(&lfs, "/");
        dump_lfs_file(&lfs, "dummy.txt");
    } else {
        printf("Failed to mount LittleFS!\n");
    }

    universal_tick_init();

    uint32_t now, loop_cnt = 0, next_blink = LED_DELAY, next_tick = TICK_DELAY;

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
        tight_loop_contents();
    }
}