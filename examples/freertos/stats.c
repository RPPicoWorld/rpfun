/**
 * @file stats.c
 * @brief FreeRTOS Task & Memory Statistics Monitor
 * @author STM32World <lth@stm32world.com>
 * @date 2026
 *
 * Copyright (c) 2026 STM32World <lth@stm32world.com>
 *
 * Periodically extracts FreeRTOS task status, runtime statistics, and heap status.
 *
 */

#include "stats.h"
#include <stdint.h>
#include <stdio.h>

#ifndef STATS_INTERVAL_MS
#define STATS_INTERVAL_MS 10000 // 10000ms = 10 seconds
#endif

static SemaphoreHandle_t g_printf_mutex = NULL;

/**
 * @brief Task implementation that periodically dumps task and system memory stats.
 */
void stats_task(void *pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();

    // Allocate formatting buffers for FreeRTOS table generation
    static char task_list_buf[512];
    static char runtime_stats_buf[512];

    while (1) {
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(STATS_INTERVAL_MS));

        // Get current system heap metrics
        size_t free_heap = xPortGetFreeHeapSize();
        size_t min_ever_free_heap = xPortGetMinimumEverFreeHeapSize();

        // Populate task state table (Name, State, Priority, Stack High Water Mark, Task Number)
        vTaskList(task_list_buf);

        // Populate runtime execution stats table (Task Name, Absolute Time spent, % CPU Time)
        vTaskGetRunTimeStats(runtime_stats_buf);

        if (g_printf_mutex != NULL) {
            if (xSemaphoreTake(g_printf_mutex, portMAX_DELAY) == pdTRUE) {
                printf("\n==================== SYSTEM STATS ====================\n");
                printf("Free Heap           : %zu bytes\n", free_heap);
                printf("Minimum Ever Free   : %zu bytes\n", min_ever_free_heap);
                printf("------------------------------------------------------\n");
                printf("Task          State  Prio  Stack  Num\n");
                printf("------------------------------------------------------\n");
                printf("%s", task_list_buf);
                printf("------------------------------------------------------\n");
                printf("Task          Abs Time         %% Time\n");
                printf("------------------------------------------------------\n");
                printf("%s", runtime_stats_buf);
                printf("======================================================\n\n");

                xSemaphoreGive(g_printf_mutex);
            }
        }
    }
}

/**
 * @brief FreeRTOS stats task initialization helper.
 */
BaseType_t stats_task_init(SemaphoreHandle_t xPrintfMutex) {
    g_printf_mutex = xPrintfMutex;

    return xTaskCreate(
        stats_task,
        "StatsTask",
        1024, /* 1KB stack allocation for formatted buffer printing */
        NULL,
        1, /* Low priority background task */
        NULL);
}
