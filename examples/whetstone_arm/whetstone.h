/**
 * @file whetstone.h
 * @brief Whetstone floating-point benchmark (single and double precision).
 * @author STM32World <lth@stm32world.com>
 * @date 2026
 *
 * Copyright (c) 2026 STM32World <lth@stm32world.com>
 */

#ifndef WHETSTONE_H
#define WHETSTONE_H

#include "pico/mutex.h"

extern mutex_t printf_mutex;

/**
 * @brief Run the Whetstone benchmark in single and double precision and print MWIPS.
 * @param core_id Core number, only used in the printout.
 */
void run_whetstone_benchmark(int core_id);

#endif // WHETSTONE_H
