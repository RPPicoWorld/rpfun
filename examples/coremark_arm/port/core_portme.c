/**
 * @file core_portme.c
 * @brief CoreMark port layer for the RP2350 (Arm and RISC-V).
 * @author STM32World <lth@stm32world.com>
 * @date 2026
 *
 * Based on the EEMBC barebones port (Apache License 2.0).
 *
 * Timing uses the 1 MHz system timer. Start/stop times and the print line buffer
 * are kept per core so both cores can run CoreMark at the same time.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "coremark.h"
#include "coremark_run.h"
#include "pico/stdlib.h"

#if VALIDATION_RUN
volatile ee_s32 seed1_volatile = 0x3415;
volatile ee_s32 seed2_volatile = 0x3415;
volatile ee_s32 seed3_volatile = 0x66;
#endif
#if PERFORMANCE_RUN
volatile ee_s32 seed1_volatile = 0x0;
volatile ee_s32 seed2_volatile = 0x0;
volatile ee_s32 seed3_volatile = 0x66;
#endif
#if PROFILE_RUN
volatile ee_s32 seed1_volatile = 0x8;
volatile ee_s32 seed2_volatile = 0x8;
volatile ee_s32 seed3_volatile = 0x8;
#endif
volatile ee_s32 seed4_volatile = ITERATIONS; // 0 = let CoreMark pick iterations for a ~10 s run
volatile ee_s32 seed5_volatile = 0;

#define EE_TICKS_PER_SEC 1000000 // time_us_32() ticks at 1 MHz

static CORETIMETYPE start_time_val[NUM_CORES];
static CORETIMETYPE stop_time_val[NUM_CORES];

void start_time(void) {
    start_time_val[get_core_num()] = time_us_32();
}

void stop_time(void) {
    stop_time_val[get_core_num()] = time_us_32();
}

CORE_TICKS get_time(void) {
    uint core = get_core_num();
    return (CORE_TICKS)(stop_time_val[core] - start_time_val[core]);
}

secs_ret time_in_secs(CORE_TICKS ticks) {
    return ((secs_ret)ticks) / (secs_ret)EE_TICKS_PER_SEC;
}

ee_u32 default_num_contexts = 1;

void portable_init(core_portable *p, int *argc, char *argv[]) {
    (void)argc;
    (void)argv;
    if (sizeof(ee_ptr_int) != sizeof(ee_u8 *)) {
        ee_printf("ERROR! Please define ee_ptr_int to a type that holds a pointer!\n");
    }
    if (sizeof(ee_u32) != 4) {
        ee_printf("ERROR! Please define ee_u32 to a 32b unsigned type!\n");
    }
    p->portable_id = 1;
}

void portable_fini(core_portable *p) {
    p->portable_id = 0;
}

void *portable_malloc(ee_size_t size) {
    return malloc(size); // The SDK malloc is protected by a mutex when pico_multicore is used
}

void portable_free(void *p) {
    free(p);
}

#define LINE_BUFFER_SIZE 160

static char line_buffer[NUM_CORES][LINE_BUFFER_SIZE];
static size_t line_length[NUM_CORES];

/**
 * @brief printf replacement used by CoreMark.
 *
 * Text is collected per core and printed one whole line at a time with a
 * "Core N:" prefix, so the output of the two cores doesn't get mixed up.
 */
int ee_printf(const char *fmt, ...) {
    uint core = get_core_num();
    char *buffer = line_buffer[core];
    size_t free_space = LINE_BUFFER_SIZE - line_length[core];

    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buffer + line_length[core], free_space, fmt, args);
    va_end(args);

    if (n < 0) {
        return n;
    }
    line_length[core] += ((size_t)n < free_space) ? (size_t)n : free_space - 1;

    char *newline = memchr(buffer, '\n', line_length[core]);
    if (newline != NULL || line_length[core] >= LINE_BUFFER_SIZE - 1) {
        mutex_enter_blocking(&printf_mutex);
        printf("Core %u: %.*s", core, (int)line_length[core], buffer);
        if (newline == NULL) {
            printf("\n");
        }
        mutex_exit(&printf_mutex);
        line_length[core] = 0;
    }
    return n;
}
