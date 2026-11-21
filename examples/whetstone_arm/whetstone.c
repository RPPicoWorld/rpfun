/**
 * @file whetstone.c
 * @brief Whetstone floating-point benchmark (single and double precision).
 * @author STM32World <lth@stm32world.com>
 * @date 2026
 *
 * Copyright (c) 2026 STM32World <lth@stm32world.com>
 *
 * Runs the classic Whetstone kernel in float and in double and reports MWIPS
 * (millions of Whetstone instructions per second). Each precision is repeated until
 * at least MIN_TIME_US has passed so slow configurations still give a stable result.
 */

#include "whetstone.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>

#include "pico/stdlib.h"

#define WHET_LOOP 10              // One pass = 100 000 * WHET_LOOP = 1 million Whetstone instructions
#define MIN_TIME_US (3 * 1000000) // Run each precision for at least 3 seconds

// Single precision instance
#define WHET_REAL float
#define WHET_NAME whetstone_float
#define WHET_SIN sinf
#define WHET_COS cosf
#define WHET_ATAN atanf
#define WHET_SQRT sqrtf
#define WHET_EXP expf
#define WHET_LOG logf
#include "whetstone_impl.h"
#undef WHET_REAL
#undef WHET_NAME
#undef WHET_SIN
#undef WHET_COS
#undef WHET_ATAN
#undef WHET_SQRT
#undef WHET_EXP
#undef WHET_LOG

// Double precision instance
#define WHET_REAL double
#define WHET_NAME whetstone_double
#define WHET_SIN sin
#define WHET_COS cos
#define WHET_ATAN atan
#define WHET_SQRT sqrt
#define WHET_EXP exp
#define WHET_LOG log
#include "whetstone_impl.h"

static volatile double whet_sink;            // Keeps the results alive
static volatile int whet_loop = WHET_LOOP;  // Read at run time so the compiler cannot precompute the benchmark

void run_whetstone_benchmark(int core_id) {
    uint32_t passes;
    uint64_t start_us, elapsed_us;

    // Single precision
    passes = 0;
    start_us = time_us_64();
    do {
        whet_sink = whetstone_float(whet_loop);
        ++passes;
        elapsed_us = time_us_64() - start_us;
    } while (elapsed_us < MIN_TIME_US);
    float mwips_float = (float)passes * (float)WHET_LOOP * 0.1f / ((float)elapsed_us / 1000000.0f);
    float check_float = (float)whet_sink;

    // Double precision
    passes = 0;
    start_us = time_us_64();
    do {
        whet_sink = whetstone_double(whet_loop);
        ++passes;
        elapsed_us = time_us_64() - start_us;
    } while (elapsed_us < MIN_TIME_US);
    float mwips_double = (float)passes * (float)WHET_LOOP * 0.1f / ((float)elapsed_us / 1000000.0f);
    double check_double = whet_sink;

    mutex_enter_blocking(&printf_mutex);
    printf("Core %d Whetstone single precision: %8.2f MWIPS (check %.6f)\n", core_id, mwips_float, check_float);
    printf("Core %d Whetstone double precision: %8.2f MWIPS (check %.6f)\n", core_id, mwips_double, check_double);
    mutex_exit(&printf_mutex);
}
