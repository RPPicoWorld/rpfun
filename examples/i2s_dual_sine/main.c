/**
 * @file main.c
 * @brief I2S Dual Sine Waveform Generator Example for RP2350 / RP2040
 * @author STM32World
 * @date 2026
 *
 * Copyright (c) 2026 STM32World
 *
 * Dual sine waveform generator using I2S on RP2350 microcontrollers.
 * Includes phase preservation and DMA memory sync optimizations to support up to 384kHz.
 *
 */

// Include necessary headers from the Pico SDK
#include "hardware/adc.h"             // For ADC access
#include "hardware/clocks.h"          // For clock frequency information
#include "hardware/dma.h"             // For DMA access
#include "hardware/gpio.h"            // For GPIO control
#include "hardware/pio.h"             // For PIO state machine
#include "hardware/structs/busctrl.h" // For bus control and priority settings
#include "hardware/sync.h"            // For mutex operations and memory barriers
#include "hardware/timer.h"           // Required for hardware timer access
#include "hardware/uart.h"            // For explicit UART configuration
#include "hardware/vreg.h"            // Needed for voltage scaling
#include "pico/bootrom.h"             // For flash command execution
#include "pico/multicore.h"           // For multicore support
#include "pico/stdlib.h"              // For sleep and stdio initialization

// Include standard I/O and Math headers
#include "math.h"
#include "stdint.h"
#include "stdio.h"

// Include generated PIO header
#include "i2s.pio.h"

#ifndef LED_DELAY
#define LED_DELAY 500 // 500ms
#endif

#ifndef TICK_DELAY
#define TICK_DELAY 1000 // 1000ms = 1 second
#endif

#ifndef RATE_CHANGE_DELAY
#define RATE_CHANGE_DELAY 10000 // 10000ms = 10 seconds
#endif

#define TAU 6.28318530717958647692
#define I2S_DMA_BUFFER_SAMPLES 64
#define I2S_DMA_BUFFER_SIZE (2 * 2 * I2S_DMA_BUFFER_SAMPLES) // 2 full buffers L+R samples
#define OUTPUT_MAX 32767

#define I2S_SD_PIN 21
#define I2S_BCK_PIN 3
#define I2S_LRCK_PIN 24

// Mutex for synchronizing access to printf and sample rate changes
auto_init_mutex(printf_mutex);

volatile uint16_t internal_temp_raw = 0;
volatile float internal_temp_c = 0.0f;

// Volatile variable to mimic STM32's uwTick
static volatile uint32_t systick = 0;

// Array of sample rates to cycle through every 10 seconds (PCM5102A supports up to 384kHz)
const uint32_t sample_rates[] = {
    8000,
    16000,
    22050,
    32000,
    44100,
    48000,
    96000,
    192000,
    384000
};

const size_t num_sample_rates = sizeof(sample_rates) / sizeof(sample_rates[0]);

// Start directly at index 0 (8000 Hz) to follow the array sequence from the start
size_t current_rate_index = 0;

// Dynamic active sample frequency, initialized from array[0]
volatile uint32_t sample_frequency = 8000;

float freq[2] = {
    110,
    220
};

float angle[2] = {
    0,
    0
};

// Will be calculated from freq
volatile float angle_change[2] = {
    0,
    0
};

float amplification[2] = {
    0.2,
    0.2
};

int16_t i2s_dma_buffer[I2S_DMA_BUFFER_SIZE];

/**
 * Shared Inter-Core Communication Pointer:
 *
 * `dma_buffer_to_fill` acts as an asynchronous job queue trigger between the hardware
 * DMA interrupt and Core 1.
 */
int16_t *volatile dma_buffer_to_fill = NULL;

float amplification_change = -0.01;

PIO pio_instance = pio0;
uint sm_instance = 0;
uint pio_offset = 0;

int dma_chan_a = -1;
int dma_chan_b = -1;

dma_channel_config ca;
dma_channel_config cb;

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
#ifdef PICO_DEFAULT_LED_PIN
    gpio_init(PICO_DEFAULT_LED_PIN);              // The LED pin is defined in the board header as PICO_DEFAULT_LED_PIN
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT); // Set the LED pin as an output
#endif
    return PICO_OK;
}

/**
 * @brief Toggles the state of the default LED.
 */
void pico_toggle_led() {
#ifdef PICO_DEFAULT_LED_PIN
    gpio_xor_mask64(((uint64_t)1 << PICO_DEFAULT_LED_PIN));
#endif
}

/**
 * @brief Processes the audio buffer to generate sine wave samples.
 * Preserves active angle phase smoothly to prevent DC offset clicks/pops on rate changes.
 * @param buffer Pointer to the buffer to fill with audio samples.
 */
void process_buffer(int16_t *buffer) {

    // Snapshot volatile angle steps locally to avoid core race updates mid-buffer
    float step0 = angle_change[0];
    float step1 = angle_change[1];

    for (int i = 0; i < 2 * I2S_DMA_BUFFER_SAMPLES; i += 2) { // Two samples left/right per step

        buffer[i] = (int16_t)(OUTPUT_MAX * amplification[0] * cosf(angle[0]));     // Left
        buffer[i + 1] = (int16_t)(OUTPUT_MAX * amplification[1] * cosf(angle[1])); // Right

        angle[0] += step0;
        angle[1] += step1;

        if (angle[0] > TAU)
            angle[0] -= TAU;
        if (angle[1] > TAU)
            angle[1] -= TAU;
    }
}

/**
 * @brief DMA Interrupt Handler.
 */
void dma_handler() {
    if (dma_hw->ints0 & (1u << dma_chan_a)) {
        dma_hw->ints0 = 1u << dma_chan_a; // Clear DMA Channel A IRQ flag

        // Point DMA channel A back to the beginning of buffer 0 for its next cycle
        dma_channel_set_read_addr(dma_chan_a, &i2s_dma_buffer[0], false);

        // Signal to Core 1 that Buffer 1 (the second half) is now idle and ready to be re-filled
        dma_buffer_to_fill = &i2s_dma_buffer[2 * I2S_DMA_BUFFER_SAMPLES];
    }
    if (dma_hw->ints0 & (1u << dma_chan_b)) {
        dma_hw->ints0 = 1u << dma_chan_b; // Clear DMA Channel B IRQ flag

        // Point DMA channel B back to the beginning of buffer 1 for its next cycle
        dma_channel_set_read_addr(dma_chan_b, &i2s_dma_buffer[2 * I2S_DMA_BUFFER_SAMPLES], false);

        // Signal to Core 1 that Buffer 0 (the first half) is now idle and ready to be re-filled
        dma_buffer_to_fill = &i2s_dma_buffer[0];
    }
}

void set_angle_changes() {
    angle_change[0] = freq[0] * (TAU / (float)sample_frequency); // left
    angle_change[1] = freq[1] * (TAU / (float)sample_frequency); // right
}

void configure_i2s_clock(uint32_t sample_rate) {
    // 32 bits per frame (16 L + 16 R) * 2 PIO clock ticks per bit cycle
    uint32_t system_clock = clock_get_hz(clk_sys);
    uint32_t bit_clock = sample_rate * 32;
    float divider = (float)system_clock / (float)(bit_clock * 2);
    pio_sm_set_clkdiv(pio_instance, sm_instance, divider);
    pio_sm_clkdiv_restart(pio_instance, sm_instance);
}

/**
 * @brief Dynamic Sample Rate Switcher (Called by Core 0).
 *
 * Halts DMA safely, resets PIO registers, updates sample calculations prior to
 * buffer generation to preserve phase, pre-fills the PIO FIFO completely to prevent
 * high-rate starvation, and cleanly triggers the audio pipeline.
 */
void set_i2s_freq(uint32_t sample_rate) {

    // 1. Disable DMA interrupts and halt active DMA channels
    irq_set_enabled(DMA_IRQ_0, false);
    dma_channel_abort(dma_chan_a);
    dma_channel_abort(dma_chan_b);

    // 2. Clear any pending DMA interrupt flags
    dma_hw->ints0 = (1u << dma_chan_a) | (1u << dma_chan_b);
    dma_buffer_to_fill = NULL;

    // 3. Disable state machine, clear FIFOs, reset SM execution logic
    pio_sm_set_enabled(pio_instance, sm_instance, false);
    pio_sm_clear_fifos(pio_instance, sm_instance);
    pio_sm_restart(pio_instance, sm_instance);

    // 4. Update sampling constants FIRST so buffer processing uses the new phase step
    sample_frequency = sample_rate;
    set_angle_changes();
    configure_i2s_clock(sample_rate);

    // 5. Force state machine Program Counter (PC) back to program entry point
    pio_sm_exec(pio_instance, sm_instance, pio_encode_jmp(pio_offset + i2s_offset_entry_point));

    // 6. Generate initial stereo samples for both double buffers with current angle state preserved
    process_buffer(&i2s_dma_buffer[0]);
    process_buffer(&i2s_dma_buffer[2 * I2S_DMA_BUFFER_SAMPLES]);

    // 7. PRE-LOAD PIO FIFO: Fill all 8 words of joined TX FIFO to avoid starvation at 192kHz/384kHz
    uint32_t *raw_32_buf = (uint32_t *)&i2s_dma_buffer[0];
    for (int i = 0; i < 8; i++) {
        pio_sm_put(pio_instance, sm_instance, raw_32_buf[i]);
    }

    // 8. Re-configure Ping-Pong DMA Channels
    dma_channel_configure(
        dma_chan_a,
        &ca,
        &pio_instance->txf[sm_instance],
        &i2s_dma_buffer[0],
        I2S_DMA_BUFFER_SAMPLES,
        false);

    dma_channel_configure(
        dma_chan_b,
        &cb,
        &pio_instance->txf[sm_instance],
        &i2s_dma_buffer[2 * I2S_DMA_BUFFER_SAMPLES],
        I2S_DMA_BUFFER_SAMPLES,
        false);

    // 9. Re-enable IRQ, enable State Machine, and launch DMA
    irq_set_enabled(DMA_IRQ_0, true);
    pio_sm_set_enabled(pio_instance, sm_instance, true);
    dma_channel_start(dma_chan_a);
}

void init_i2s_pio_dma() {

    // Elevate DMA bus priority over CPU cores to prevent SRAM contention latency glitches
    busctrl_hw->priority = BUSCTRL_BUS_PRIORITY_DMA_W_BITS | BUSCTRL_BUS_PRIORITY_DMA_R_BITS;

    pio_offset = pio_add_program(pio_instance, &i2s_program);
    pio_sm_config c = i2s_config(pio_offset);

    sm_config_set_out_pins(&c, I2S_SD_PIN, 1);
    sm_config_set_sideset_pins(&c, I2S_BCK_PIN);
    sm_config_set_set_pins(&c, I2S_LRCK_PIN, 1);

    pio_gpio_init(pio_instance, I2S_SD_PIN);
    pio_gpio_init(pio_instance, I2S_BCK_PIN);
    pio_gpio_init(pio_instance, I2S_LRCK_PIN);

    pio_sm_set_consecutive_pindirs(pio_instance, sm_instance, I2S_SD_PIN, 1, true);
    pio_sm_set_consecutive_pindirs(pio_instance, sm_instance, I2S_BCK_PIN, 1, true);
    pio_sm_set_consecutive_pindirs(pio_instance, sm_instance, I2S_LRCK_PIN, 1, true);

    sm_config_set_out_shift(&c, false, true, 32); // Shift MSB first, autopull at 32 bits
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);

    pio_sm_init(pio_instance, sm_instance, pio_offset + i2s_offset_entry_point, &c);

    // Sync initial sampling frequency from index 0
    sample_frequency = sample_rates[current_rate_index];

    // Configure clock divider for default startup frequency
    configure_i2s_clock(sample_frequency);

    // Pre-fill audio buffer before enabling DMA to avoid starting with dead silence/underrun
    process_buffer(&i2s_dma_buffer[0]);
    process_buffer(&i2s_dma_buffer[2 * I2S_DMA_BUFFER_SAMPLES]);

    // Setup Ping-Pong DMA
    dma_chan_a = dma_claim_unused_channel(true);
    dma_chan_b = dma_claim_unused_channel(true);

    ca = dma_channel_get_default_config(dma_chan_a);
    channel_config_set_transfer_data_size(&ca, DMA_SIZE_32);
    channel_config_set_read_increment(&ca, true);
    channel_config_set_write_increment(&ca, false);
    channel_config_set_dreq(&ca, pio_get_dreq(pio_instance, sm_instance, true));
    channel_config_set_chain_to(&ca, dma_chan_b);

    dma_channel_configure(
        dma_chan_a,
        &ca,
        &pio_instance->txf[sm_instance],
        &i2s_dma_buffer[0],
        I2S_DMA_BUFFER_SAMPLES,
        false);

    cb = dma_channel_get_default_config(dma_chan_b);
    channel_config_set_transfer_data_size(&cb, DMA_SIZE_32);
    channel_config_set_read_increment(&cb, true);
    channel_config_set_write_increment(&cb, false);
    channel_config_set_dreq(&cb, pio_get_dreq(pio_instance, sm_instance, true));
    channel_config_set_chain_to(&cb, dma_chan_a);

    dma_channel_configure(
        dma_chan_b,
        &cb,
        &pio_instance->txf[sm_instance],
        &i2s_dma_buffer[2 * I2S_DMA_BUFFER_SAMPLES],
        I2S_DMA_BUFFER_SAMPLES,
        false);

    dma_set_irq0_channel_mask_enabled((1u << dma_chan_a) | (1u << dma_chan_b), true);
    irq_set_exclusive_handler(DMA_IRQ_0, dma_handler);
    irq_set_enabled(DMA_IRQ_0, true);

    // Pre-load FIFO during initial startup
    uint32_t *raw_32_buf = (uint32_t *)&i2s_dma_buffer[0];
    for (int i = 0; i < 8; i++) {
        pio_sm_put(pio_instance, sm_instance, raw_32_buf[i]);
    }

    pio_sm_set_enabled(pio_instance, sm_instance, true);

    // Explicitly trigger the initial DMA channel transfer
    dma_channel_start(dma_chan_a);
}

/**
 * @brief Entry point for Core 1 (Dedicated Audio DSP Worker).
 */
void core1_entry() {

    mutex_enter_blocking(&printf_mutex); // Synchronize with Core 0 for printing
    printf("Core 1: Booting audio calculation worker...\n");
    mutex_exit(&printf_mutex);

    uint32_t now, loop_cnt = 0, next_tick = TICK_DELAY + (TICK_DELAY / 2); // Start Core 1's ticks offset from Core 0

    // Main loop for Core 1
    while (1) {

        now = systick;

        if (now >= next_tick) {
            mutex_enter_blocking(&printf_mutex);
            printf("Core 1 tick %lu (loop = %lu)\n", now, loop_cnt);
            mutex_exit(&printf_mutex);
            loop_cnt = 0;
            next_tick = now + TICK_DELAY;
        }

        /**
         * Lockless atomic transfer with Data Memory Barrier (__dmb) to avoid
         * inter-core cache or store buffer delay stalls.
         */
        if (dma_buffer_to_fill != NULL) {
            int16_t *buf = dma_buffer_to_fill;
            dma_buffer_to_fill = NULL; // Acknowledge request immediately

            __dmb(); // Force write visibility across bus

            if (buf) {
                process_buffer(buf);
            }
        }

        ++loop_cnt;

        // Give the memory bus and Core 0 a chance to breathe
        tight_loop_contents();
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

    mutex_enter_blocking(&printf_mutex);
    printf("\n\n\nCore 0: Booting...\n");
    printf("Running on %s at %d MHz\n",
#ifdef __riscv
           "RISC-V",
#else
           "Arm Cortex-M33",
#endif
           frequency_count_khz(CLOCKS_FC0_SRC_VALUE_CLK_SYS) / 1000);
    printf("Starting I2S dynamic sample rate demo\n");
    mutex_exit(&printf_mutex);

    // Start the heartbeat (Cross-Platform)
    universal_tick_init();

    // Launch core1_entry function on Core 1
    multicore_launch_core1(core1_entry);

    set_angle_changes();
    init_i2s_pio_dma();

    uint32_t now = 0;
    uint32_t next_blink = LED_DELAY;
    uint32_t next_tick = TICK_DELAY;
    uint32_t next_rate_change = RATE_CHANGE_DELAY;
    uint32_t loop_cnt = 0;

    // Main loop for Core 0
    while (true) {

        now = systick;

        if (now >= next_blink) {
            pico_toggle_led();
            next_blink = now + LED_DELAY;
        }

        if (now >= next_tick) {

            mutex_enter_blocking(&printf_mutex); // Ensure we've got exclusive access to printf
            printf("Core 0 tick %lu (loop = %lu, rate = %lu Hz)\n", now, loop_cnt, sample_frequency);
            mutex_exit(&printf_mutex);
            loop_cnt = 0;
            next_tick = now + TICK_DELAY;
        }

        /**
         * Every 10 Seconds: Cycle to the next sample rate sequentially starting from index 0
         */
        if (now >= next_rate_change) {
            current_rate_index = (current_rate_index + 1) % num_sample_rates;
            uint32_t new_rate = sample_rates[current_rate_index];

            mutex_enter_blocking(&printf_mutex);
            printf("\n*** Switching Sample Rate to %lu Hz ***\n\n", new_rate);
            mutex_exit(&printf_mutex);

            // Reconfigure clock and buffers
            set_i2s_freq(new_rate);

            next_rate_change = now + RATE_CHANGE_DELAY;
        }

        ++loop_cnt;

        tight_loop_contents(); // Allow other tasks to run and prevent CPU hogging
    }
}

// vim: ts=4 et nowrap