/**
 * @file main.c
 * @brief Handling WS1812 LED strip with PIO on the RP2350
 * @author STM32World <lth@stm32world.com>
 * @date 2026
 *
 * Copyright (c) 2026 STM32World <lth@stm32world.com>
 *
 * Handling WS1812 LED strip with PIO on the RP2350, along with a simple LED blink and tick example.
 * The ws2812 PIO program is auto-generated from ws2812.pio and included in the build.
 *
 */

// Include necessary headers from the Pico SDK

#include "hardware/clocks.h" // For clock frequency information
#include "hardware/dma.h"    // For DMA access (if needed)
#include "hardware/gpio.h"   // For GPIO control
#include "hardware/pio.h"    // For PIO access
#include "hardware/timer.h"  // Required for hardware timer access
#include "hardware/vreg.h"   // Needed for voltage scaling
#include "pico/multicore.h"  // For multicore support
#include "pico/stdlib.h"     // For sleep and stdio initialization

// Include standard I/O for printf
#include <stdint.h>
#include <stdio.h>

#include "can2040.h" // Include CAN2040 header for CAN bus functionality

// Define timing constants for LED blinking and tick reporting
#define LED_DELAY 500   // 500ms
#define TICK_DELAY 1000 // 1000ms = 1 second

// WS2812 LED configuration
#define NUM_LEDS 64
#define WS2812_PIN 29
#define IS_RGBW false

// Max brightness factor (0 = completely off, 255 = 100% full brightness)
// WS2812s at 100% white pull ~3.8A for 64 LEDs. Setting this lower saves power.
#define MAX_BRIGHTNESS 64 // ~25% brightness

// Timing constants for WS2812 demo and pattern switching
#define WS2812_FRAME_DELAY 20    // 20ms update interval (~50 FPS)
#define PATTERN_SWAP_DELAY 10000 // Switch pattern every 10 seconds

// Volatile variable to mimic STM32's uwTick
static volatile uint32_t systick = 0;

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

/**
 * @brief Converts RGB values to a 32-bit GRB format for WS2812.
 * @param r Red value (0-255)
 * @param g Green value (0-255)
 * @param b Blue value (0-255)
 * @return 32-bit GRB color value
 */
static inline uint32_t urgb_u32(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)(g) << 16) |
           ((uint32_t)(r) << 8) |
           (uint32_t)(b);
}

/**
 * @brief Sends a pixel color to the WS2812 LED strip using PIO with brightness scaling.
 * @param pio The PIO instance to use (pio0 or pio1)
 * @param sm The state machine number to use (0-3)
 * @param pixel_grb The 32-bit GRB color value to send
 */
static inline void put_pixel(PIO pio, uint sm, uint32_t pixel_grb) {

#if defined(MAX_BRIGHTNESS) && (MAX_BRIGHTNESS < 255)
    // Extract color channels
    uint8_t g = (pixel_grb >> 16) & 0xFF;
    uint8_t r = (pixel_grb >> 8) & 0xFF;
    uint8_t b = pixel_grb & 0xFF;

    // Scale each channel by MAX_BRIGHTNESS / 256
    g = (g * MAX_BRIGHTNESS) >> 8;
    r = (r * MAX_BRIGHTNESS) >> 8;
    b = (b * MAX_BRIGHTNESS) >> 8;

    // Repack scaled GRB value
    pixel_grb = urgb_u32(r, g, b);
#endif

    // WS2812 expects bits aligned to the upper 24 bits of a 32-bit word
    pio_sm_put_blocking(pio, sm, pixel_grb << 8u);
}

/**
 * @brief Generates a color from a 0-255 position on a rainbow wheel.
 * @param pos Position on the wheel (0-255)
 * @return 32-bit GRB color value
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
 * @brief Non-blocking WS2812 State Machine & Animation Handler
 * @param pio The PIO instance to use (pio0 or pio1)
 * @param sm The state machine number to use (0-3)
 * @param now The current time in milliseconds
 */
void ws2812_demo_update(PIO pio, uint sm, uint32_t now) {
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
            put_pixel(pio, sm, wheel_color(color_pos));
        }
        state_counter += 2;
        break;
    }

    case 1: {
        // --- PATTERN 1: Single Pixel Chaser with Fade Tail ---
        uint16_t head = state_counter % NUM_LEDS;
        for (int i = 0; i < NUM_LEDS; i++) {
            if (i == head) {
                put_pixel(pio, sm, urgb_u32(255, 255, 255)); // Bright White head
            } else if (i == (head + NUM_LEDS - 1) % NUM_LEDS) {
                put_pixel(pio, sm, urgb_u32(0, 0, 150)); // Blue tail 1
            } else if (i == (head + NUM_LEDS - 2) % NUM_LEDS) {
                put_pixel(pio, sm, urgb_u32(0, 0, 40)); // Dim Blue tail 2
            } else {
                put_pixel(pio, sm, urgb_u32(0, 0, 0)); // Off
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
            active_color = urgb_u32(60, 0, 0); // Red
        else if (color_idx == 1)
            active_color = urgb_u32(0, 60, 0); // Green
        else
            active_color = urgb_u32(0, 0, 60); // Blue

        for (int i = 0; i < NUM_LEDS; i++) {
            if (i < fill_len) {
                put_pixel(pio, sm, active_color);
            } else {
                put_pixel(pio, sm, urgb_u32(0, 0, 0));
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
            brightness = phase; // Fade In
        } else {
            brightness = 255 - phase; // Fade Out
        }

        // Scale brightness down slightly to save current (~ max 100/255)
        brightness = (brightness * 100) / 128;

        uint32_t pulse_color = urgb_u32(brightness, brightness / 2, 0); // Warm Gold

        for (int i = 0; i < NUM_LEDS; i++) {
            put_pixel(pio, sm, pulse_color);
        }
        state_counter += 2;
        break;
    }
    }
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

    // Start the heartbeat (Cross-Platform)
    universal_tick_init();

    // Select PIO instance and state machine
    PIO pio = pio0;
    int sm = 0;
    uint offset = pio_add_program(pio, &ws2812_program);

    // Initialize PIO for GPIO 29 at 800kHz target frequency
    ws2812_program_init(pio, sm, offset, WS2812_PIN, 800000, IS_RGBW);

    uint32_t now, loop_cnt = 0, next_blink = LED_DELAY, next_tick = TICK_DELAY;

    // Main loop for Core 0
    while (true) {

        now = systick;

        ws2812_demo_update(pio, sm, now);

        if (now >= next_blink) {
            pico_toggle_led();
            next_blink = now + LED_DELAY;
        }

        if (now >= next_tick) {
            printf("Core 0 tick %lu (loop = %lu)\n", now, loop_cnt);
            loop_cnt = 0;
            next_tick = now + TICK_DELAY;
        }

        ++loop_cnt;

        tight_loop_contents(); // Allow other tasks to run and prevent CPU hogging
    }
}

// vim: ts=4 et nowrap
