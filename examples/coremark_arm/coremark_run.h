/**
 * @file coremark_run.h
 * @brief Entry point for running EEMBC CoreMark from main.c.
 * @author STM32World <lth@stm32world.com>
 * @date 2026
 *
 * Copyright (c) 2026 STM32World <lth@stm32world.com>
 *
 * CoreMark's own main() in coremark/core_main.c is renamed to coremark_main() at
 * compile time (see CMakeLists.txt), so the EEMBC files stay unmodified.
 */

#ifndef COREMARK_RUN_H
#define COREMARK_RUN_H

#include "pico/mutex.h"

extern mutex_t printf_mutex;

/**
 * @brief Run one complete CoreMark (about 10 seconds) on the calling core and print the result.
 * @return Always 0.
 */
int coremark_main(void);

#endif // COREMARK_RUN_H
