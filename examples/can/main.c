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

// Volatile variable to mimic STM32's uwTick
static volatile uint32_t systick = 0;

// Simple example of irq safe queue (this is not multi-core safe)
#define QUEUE_SIZE 128 // Must be power of 2
static struct {
    uint32_t pull_pos;
    volatile uint32_t push_pos;
    struct can2040_msg queue[QUEUE_SIZE];
} MessageQueue;

static struct can2040 cbus;

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

// Main canbus callback (called from irq handler)
static void can2040_cb(struct can2040 *cd, uint32_t notify, struct can2040_msg *msg) {
    // printf("CAN2040 callback: notify=%lu, id=0x%08lx, dlc=%lu\n", notify, msg->id, msg->dlc);
    if (notify == CAN2040_NOTIFY_RX) {

        // Extract flags
        bool is_rtr = (msg->id & CAN2040_ID_RTR) != 0;
        bool is_ext = (msg->id & CAN2040_ID_EFF) != 0;

        // Clean ID (mask off RTR and EFF bits)
        uint32_t clean_id = is_ext ? (msg->id & 0x1FFFFFFF) : (msg->id & 0x7FF);

        printf("RP CAN RX: ID=0x%08lx [%s] [%s] DLC=%lu\n", clean_id, is_ext ? "EXT" : "STD", is_rtr ? "RTR" : "DATA", msg->dlc);

        // Example message filter
        uint32_t id = msg->id;
        if (id < 0x101 || id > 0x201)
            return;

        // Add to queue
        uint32_t push_pos = MessageQueue.push_pos;
        uint32_t pull_pos = MessageQueue.pull_pos;
        if (push_pos + 1 == pull_pos)
            // No space in queue
            return;
        MessageQueue.queue[push_pos % QUEUE_SIZE] = *msg;
        MessageQueue.push_pos = push_pos + 1;
    }
}

// PIO interrupt handler
static void PIOx_IRQHandler(void) {
    can2040_pio_irq_handler(&cbus);
}

// Initialize the can2040 module
void canbus_setup(void) {
    uint32_t pio_num = 0;
    uint32_t sys_clock = SYS_CLK_HZ, bitrate = 1000000;
    uint32_t gpio_rx = 21, gpio_tx = 19;

    // Setup canbus
    can2040_setup(&cbus, pio_num);
    can2040_callback_config(&cbus, can2040_cb);

    // Enable irqs
    irq_set_exclusive_handler(PIO0_IRQ_0, PIOx_IRQHandler);
    irq_set_priority(PIO0_IRQ_0, 1);
    irq_set_enabled(PIO0_IRQ_0, 1);

    // Start canbus
    can2040_start(&cbus, sys_clock, bitrate, gpio_rx, gpio_tx);
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

    canbus_setup();

    uint32_t now, loop_cnt = 0, next_blink = LED_DELAY, next_tick = TICK_DELAY;

    // Main loop for Core 0
    while (true) {

        now = systick;

        // ws2812_demo_update(pio, sm, now);

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
