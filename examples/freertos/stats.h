/**
 * @file stats.h
 * @brief FreeRTOS Task & Memory Statistics Monitor
 * @author STM32World <lth@stm32world.com>
 * @date 2026
 *
 * Copyright (c) 2026 STM32World <lth@stm32world.com>
 *
 * Provides periodic monitoring of tasks, execution time, and stack/heap usage.
 *
 */

#ifndef STATS_H
#define STATS_H

#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initializes and creates the FreeRTOS statistics monitoring task.
 * * @param xPrintfMutex Handle to the mutex used for synchronizing UART/printf calls.
 * @return BaseType_t pdPASS if the task was created successfully, pdFAIL otherwise.
 */
BaseType_t stats_task_init(SemaphoreHandle_t xPrintfMutex);

/**
 * @brief Task function that runs every 10 seconds to print runtime and stack statistics.
 * * @param pvParameters Unused task parameters.
 */
void stats_task(void *pvParameters);

#ifdef __cplusplus
}
#endif

#endif /* STATS_H */