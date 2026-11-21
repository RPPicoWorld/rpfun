/**
 * @file core_portme.h
 * @brief CoreMark port configuration for the RP2350 (Arm and RISC-V).
 * @author STM32World <lth@stm32world.com>
 * @date 2026
 *
 * Based on the EEMBC barebones port (Apache License 2.0).
 *
 * Every core runs its own single-context copy of CoreMark (MULTITHREAD = 1), so
 * both cores can run the benchmark at the same time. The data block comes from
 * the heap so each core gets its own, and the timing and print state is kept per
 * core in core_portme.c.
 */

#ifndef CORE_PORTME_H
#define CORE_PORTME_H

#include <stddef.h>

#define HAS_FLOAT 1   // Hardware FPU on Arm, soft float on RISC-V; used only for the score printout
#define HAS_TIME_H 0  // Timing comes from the RP2350 microsecond timer
#define USE_CLOCK 0
#define HAS_STDIO 1
#define HAS_PRINTF 0  // ee_printf is implemented in core_portme.c (adds a "Core N:" prefix)

#ifndef COMPILER_VERSION
#ifdef __GNUC__
#define COMPILER_VERSION "GCC" __VERSION__
#else
#define COMPILER_VERSION "unknown"
#endif
#endif

#ifndef COMPILER_FLAGS
#define COMPILER_FLAGS FLAGS_STR // FLAGS_STR is set from CMakeLists.txt
#endif

#ifndef MEM_LOCATION
#define MEM_LOCATION "HEAP"
#endif

typedef signed short ee_s16;
typedef unsigned short ee_u16;
typedef signed int ee_s32;
typedef double ee_f32;
typedef unsigned char ee_u8;
typedef unsigned int ee_u32;
typedef ee_u32 ee_ptr_int;
typedef size_t ee_size_t;

#define align_mem(x) (void *)(4 + (((ee_ptr_int)(x) - 1) & ~3))

#define CORETIMETYPE ee_u32
typedef ee_u32 CORE_TICKS; // Microseconds; wraps after ~71 minutes, far longer than a run

#define SEED_METHOD SEED_VOLATILE
#define MEM_METHOD MEM_MALLOC

#define MULTITHREAD 1
#define USE_PTHREAD 0
#define USE_FORK 0
#define USE_SOCKET 0

#define MAIN_HAS_NOARGC 1
#define MAIN_HAS_NORETURN 0

extern ee_u32 default_num_contexts;

typedef struct CORE_PORTABLE_S {
    ee_u8 portable_id;
} core_portable;

void portable_init(core_portable *p, int *argc, char *argv[]);
void portable_fini(core_portable *p);

#if !defined(PROFILE_RUN) && !defined(PERFORMANCE_RUN) && !defined(VALIDATION_RUN)
#if (TOTAL_DATA_SIZE == 1200)
#define PROFILE_RUN 1
#elif (TOTAL_DATA_SIZE == 2000)
#define PERFORMANCE_RUN 1
#else
#define VALIDATION_RUN 1
#endif
#endif

int ee_printf(const char *fmt, ...);

#endif // CORE_PORTME_H
