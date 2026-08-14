/**
 * @file main.c
 * @brief Overclocking RP2350 with DMA-driven WS2812 Animations
 * @author STM32World <lth@stm32world.com>
 * @date 2026
 *
 * Copyright (c) 2026 STM32World <lth@stm32world.com>
 *
 * RP2350 superloop using DMA to stream 64 WS2812 LEDs on GPIO 29 via PIO0 SM0.
 */

#include "hardware/clocks.h" // For clock frequency information
#include "hardware/dma.h"    // For DMA access
#include "hardware/gpio.h"   // For GPIO control
#include "hardware/pio.h"    // For PIO access
#include "hardware/timer.h"  // Required for hardware timer access
#include "hardware/vreg.h"   // Needed for voltage scaling
#include "pico/bootrom.h"    // For flash command execution
#include "pico/multicore.h"  // For multicore support
#include "pico/stdlib.h"     // For sleep and stdio initialization
#include "ws2812.pio.h"      // Auto-generated from ws2812.pio

#include <stdint.h>
#include <stdio.h>

#ifndef LED_DELAY
#define LED_DELAY 500 // 500ms
#endif

#ifndef TICK_DELAY
#define TICK_DELAY 1000 // 1000ms = 1 second
#endif

#define NUM_LEDS 64
#define WS2812_PIN 29
#define IS_RGBW false

#define WS2812_FRAME_DELAY 20   // 20ms update interval (~50 FPS)
#define PATTERN_SWAP_DELAY 5000 // Switch pattern every 5 seconds

// Max brightness factor (0 = off, 255 = 100% full brightness)
#define MAX_BRIGHTNESS 128 // ~25% brightness

// Mutex for synchronizing access to printf
auto_init_mutex(printf_mutex);

// Volatile variable to mimic STM32's uwTick
static volatile uint32_t systick = 0;

// --- DMA Globals ---
static uint32_t led_buffer[NUM_LEDS];
static int dma_chan = -1;

/**
 * @brief Callback for the repeating timer.
 */
bool on_timer_tick(struct repeating_timer *t) {
    systick++;
    return true;
}

/**
 * @brief Universal tick initialization using the SDK timer pool.
 */
void universal_tick_init() {
    static struct repeating_timer timer;
    add_repeating_timer_ms(-1, on_timer_tick, NULL, &timer);
}

// Perform initialisation
int pico_led_init(void) {
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    return PICO_OK;
}

/**
 * @brief Toggles the state of the default LED.
 */
void pico_toggle_led() {
    gpio_xor_mask64(((uint64_t)1 << PICO_DEFAULT_LED_PIN));
}

// Helper function to format 24-bit GRB colors for WS2812
static inline uint32_t urgb_u32(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)(g) << 16) |
           ((uint32_t)(r) << 8) |
           (uint32_t)(b);
}

/**
 * @brief Writes a color into the RAM frame buffer (led_buffer).
 */
static inline void set_pixel_buffer(uint index, uint32_t pixel_grb) {
    if (index >= NUM_LEDS)
        return;

#if defined(MAX_BRIGHTNESS) && (MAX_BRIGHTNESS < 255)
    uint8_t g = (pixel_grb >> 16) & 0xFF;
    uint8_t r = (pixel_grb >> 8) & 0xFF;
    uint8_t b = pixel_grb & 0xFF;

    g = (g * MAX_BRIGHTNESS) >> 8;
    r = (r * MAX_BRIGHTNESS) >> 8;
    b = (b * MAX_BRIGHTNESS) >> 8;

    pixel_grb = urgb_u32(r, g, b);
#endif

    // Align bits to upper 24 bits for WS2812 PIO program
    led_buffer[index] = pixel_grb << 8u;
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
 * @brief Triggers a non-blocking DMA transfer of led_buffer to PIO.
 */
void ws2812_dma_render() {
    if (dma_channel_is_busy(dma_chan)) {
        return; // Skip if previous transfer is still in progress
    }
    // Re-arm read pointer to start of led_buffer and trigger
    dma_channel_set_read_addr(dma_chan, led_buffer, true);
}

/**
 * @brief Generates a color from a 0-255 position on a rainbow wheel.
 */
static uint32_t wheel_color(uint8_t pos) {
    pos = 255 - pos;
    if (pos < 85) {
        return urgb_u32(255 - pos * 3, 0, pos * 3);
    } else if (pos < 170) {
        pos -= 85;
        return urgb_u32(0, pos * 3, 255 - pos * 3);
    } else {
        pos -= 170;
        return urgb_u32(pos * 3, 255 - pos * 3, 0);
    }
}

/**
 * @brief Non-blocking WS2812 State Machine & Animation Handler (DMA driven)
 */
void ws2812_demo_update(uint32_t now) {
    static uint32_t next_frame = 0;
    static uint32_t next_pattern = PATTERN_SWAP_DELAY;
    static uint8_t pattern_mode = 0;
    static uint16_t state_counter = 0;

    // Check if it's time to switch pattern mode
    if (now >= next_pattern) {
        pattern_mode = (pattern_mode + 1) % 4;
        state_counter = 0;
        next_pattern = now + PATTERN_SWAP_DELAY;
    }

    // Check if it's time to render a new frame (~50 FPS)
    if (now < next_frame) {
        return;
    }
    next_frame = now + WS2812_FRAME_DELAY;

    switch (pattern_mode) {
    case 0: {
        // --- PATTERN 0: Rainbow Wave ---
        for (int i = 0; i < NUM_LEDS; i++) {
            uint8_t color_pos = (i * 256 / NUM_LEDS + state_counter) & 0xFF;
            set_pixel_buffer(i, wheel_color(color_pos));
        }
        state_counter += 2;
        break;
    }

    case 1: {
        // --- PATTERN 1: Single Pixel Chaser with Fade Tail ---
        uint16_t head = state_counter % NUM_LEDS;
        for (int i = 0; i < NUM_LEDS; i++) {
            if (i == head) {
                set_pixel_buffer(i, urgb_u32(255, 255, 255));
            } else if (i == (head + NUM_LEDS - 1) % NUM_LEDS) {
                set_pixel_buffer(i, urgb_u32(0, 0, 150));
            } else if (i == (head + NUM_LEDS - 2) % NUM_LEDS) {
                set_pixel_buffer(i, urgb_u32(0, 0, 40));
            } else {
                set_pixel_buffer(i, urgb_u32(0, 0, 0));
            }
        }
        state_counter++;
        break;
    }

    case 2: {
        // --- PATTERN 2: Color Wipe ---
        uint16_t fill_len = (state_counter / 2) % (NUM_LEDS + 1);
        uint8_t color_idx = (state_counter / (NUM_LEDS * 2)) % 3;
        uint32_t active_color;

        if (color_idx == 0)
            active_color = urgb_u32(255, 0, 0);
        else if (color_idx == 1)
            active_color = urgb_u32(0, 255, 0);
        else
            active_color = urgb_u32(0, 0, 255);

        for (int i = 0; i < NUM_LEDS; i++) {
            if (i < fill_len) {
                set_pixel_buffer(i, active_color);
            } else {
                set_pixel_buffer(i, urgb_u32(0, 0, 0));
            }
        }
        state_counter++;
        break;
    }

    case 3: {
        // --- PATTERN 3: Breathing Pulse ---
        uint8_t brightness;
        uint8_t phase = state_counter & 0xFF;

        if (phase < 128) {
            brightness = phase * 2;
        } else {
            brightness = (255 - phase) * 2;
        }

        uint32_t pulse_color = urgb_u32(brightness, brightness / 2, 0);

        for (int i = 0; i < NUM_LEDS; i++) {
            set_pixel_buffer(i, pulse_color);
        }
        state_counter += 2;
        break;
    }
    }

    // Trigger hardware DMA push to PIO
    ws2812_dma_render();
}

/**
 * @brief Main entry point for Core 0.
 */
int main() {

    int rc = pico_led_init();
    hard_assert(rc == PICO_OK);

    stdio_init_all();

    uart_set_baudrate(uart0, 921600);
    sleep_ms(50);

    mutex_enter_blocking(&printf_mutex);
    printf("\n\n\nCore 0: Booting...\n");
    printf("Running on %s at %d MHz\n",
#ifdef __riscv
           "RISC-V",
#else
           "Arm Cortex-M33",
#endif
           frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_SYS) / 1000);
    mutex_exit(&printf_mutex);

    // Start tick timer
    universal_tick_init();

    // Select PIO instance and state machine
    PIO pio = pio0;
    int sm = 0;
    uint offset = pio_add_program(pio, &ws2812_program);

    // Initialize PIO for GPIO 29 at 800kHz target frequency
    ws2812_program_init(pio, sm, offset, WS2812_PIN, 800000, IS_RGBW);

    // Initialize DMA hardware for PIO0 State Machine 0
    ws2812_dma_init(pio, sm);

    uint32_t now, loop_cnt = 0, next_blink = LED_DELAY, next_tick = TICK_DELAY;

    // Main loop for Core 0
    while (true) {

        now = systick;

        // Non-blocking WS2812 DMA render update
        ws2812_demo_update(now);

        // Default LED Toggle Task
        if (now >= next_blink) {
            pico_toggle_led();
            next_blink = now + LED_DELAY;
        }

        // Periodic Tick Print Task
        if (now >= next_tick) {
            mutex_enter_blocking(&printf_mutex);
            printf("Core 0 tick %lu (loop = %lu)\n", now, loop_cnt);
            mutex_exit(&printf_mutex);
            loop_cnt = 0;
            next_tick = now + TICK_DELAY;
        }

        ++loop_cnt;

        tight_loop_contents();
    }
}

// vim: ts=4 et nowrap