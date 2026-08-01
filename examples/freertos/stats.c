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

    // Track previous runtime counts across iterations for accurate deltas
    static uint32_t u32LastTaskRunTimes[16] = { 0 };
    static uint64_t u64LastWallClockUs = 0;

    while (1) {
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(STATS_INTERVAL_MS));

        UBaseType_t uxTaskCount = uxTaskGetNumberOfTasks();
        TaskStatus_t *pxTaskStatusArray = pvPortMalloc(uxTaskCount * sizeof(TaskStatus_t));

        if (pxTaskStatusArray != NULL) {
            uint32_t ulTotalRunTime;
            uxTaskCount = uxTaskGetSystemState(pxTaskStatusArray, uxTaskCount, &ulTotalRunTime);

            // Calculate exact elapsed wall-clock microseconds since last check
            uint64_t u64CurrentWallClockUs = time_us_64();
            uint64_t u64ElapsedUs = u64CurrentWallClockUs - u64LastWallClockUs;
            u64LastWallClockUs = u64CurrentWallClockUs;

            if (g_printf_mutex != NULL && xSemaphoreTake(g_printf_mutex, portMAX_DELAY) == pdTRUE) {
                printf("\n==================== SYSTEM STATS ====================\n");
                printf("Free Heap           : %zu bytes\n", xPortGetFreeHeapSize());
                printf("Minimum Ever Free   : %zu bytes\n", xPortGetMinimumEverFreeHeapSize());
                printf("------------------------------------------------------\n");
                printf("Task          State  Prio  Stack  Num  Affinity  %% Core\n");
                printf("------------------------------------------------------\n");

                uint32_t u32TotalCoreUsagePct = 0;

                for (UBaseType_t i = 0; i < uxTaskCount; i++) {
                    // Task index bounds safety
                    UBaseType_t tNum = pxTaskStatusArray[i].xTaskNumber;

                    // Compute delta execution microseconds spent in this 10s window
                    uint32_t u32CurrentTaskRunTime = pxTaskStatusArray[i].ulRunTimeCounter;
                    uint32_t u32DeltaTaskUs = u32CurrentTaskRunTime - u32LastTaskRunTimes[tNum];
                    u32LastTaskRunTimes[tNum] = u32CurrentTaskRunTime;

                    // Calculate percentage relative to ONE single core (100% max per core)
                    uint32_t u32CorePct = 0;
                    if (u64ElapsedUs > 0) {
                        u32CorePct = (uint32_t)(((uint64_t)u32DeltaTaskUs * 100ULL) / u64ElapsedUs);
                    }
                    u32TotalCoreUsagePct += u32CorePct;

                    // Convert state enum to character flag
                    char cState = 'X';
                    switch (pxTaskStatusArray[i].eCurrentState) {
                    case eReady:
                        cState = 'R';
                        break;
                    case eBlocked:
                        cState = 'B';
                        break;
                    case eSuspended:
                        cState = 'S';
                        break;
                    case eDeleted:
                        cState = 'D';
                        break;
                    default:
                        break;
                    }

                    printf("%-13s %c     %2lu    %5u  %2lu   0x%08lx  %3lu%%\n", pxTaskStatusArray[i].pcTaskName, cState, pxTaskStatusArray[i].uxCurrentPriority, pxTaskStatusArray[i].usStackHighWaterMark, pxTaskStatusArray[i].xTaskNumber, pxTaskStatusArray[i].uxCoreAffinityMask, u32CorePct);
                }

                printf("------------------------------------------------------\n");
                printf("TOTAL DUAL-CORE LOAD (Max 200%%)             : %3lu%%\n", u32TotalCoreUsagePct);
                printf("======================================================\n\n");

                xSemaphoreGive(g_printf_mutex);
            }

            vPortFree(pxTaskStatusArray);
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
        256, //  256-word stack allocation for formatted buffer printing
        NULL,
        1, /* Low priority background task */
        NULL);
}
