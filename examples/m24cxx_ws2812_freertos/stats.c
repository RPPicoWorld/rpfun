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
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#ifndef STATS_INTERVAL_MS
#define STATS_INTERVAL_MS 10000 // 10000ms = 10 seconds
#endif

static SemaphoreHandle_t g_printf_mutex = NULL;

// Helper structure to map Task Numbers safely without out-of-bounds access
typedef struct {
    UBaseType_t xTaskNumber;
    uint32_t ulLastRunTime;
} TaskHistory_t;

#define MAX_TRACKED_TASKS 32

/**
 * @brief Task implementation that periodically dumps task and system memory stats.
 */
void stats_task(void *pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();

    // Track task runtime history safely using structure lookup
    static TaskHistory_t xTaskHistory[MAX_TRACKED_TASKS] = { 0 };
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

            // Step 1: Purge history entries for tasks that no longer exist in the system
            for (int h = 0; h < MAX_TRACKED_TASKS; h++) {
                if (xTaskHistory[h].xTaskNumber != 0) {
                    bool bTaskStillExists = false;
                    for (UBaseType_t i = 0; i < uxTaskCount; i++) {
                        if (pxTaskStatusArray[i].xTaskNumber == xTaskHistory[h].xTaskNumber) {
                            bTaskStillExists = true;
                            break;
                        }
                    }
                    if (!bTaskStillExists) {
                        xTaskHistory[h].xTaskNumber = 0; // Free up slot for new tasks
                        xTaskHistory[h].ulLastRunTime = 0;
                    }
                }
            }

            if (g_printf_mutex != NULL && xSemaphoreTake(g_printf_mutex, portMAX_DELAY) == pdTRUE) {
                printf("\n================================= SYSTEM STATS =================================\n");
                printf("Free Heap         : %zu bytes\n", xPortGetFreeHeapSize());
                printf("Minimum Ever Free : %zu bytes\n", xPortGetMinimumEverFreeHeapSize());
                printf("--------------------------------------------------------------------------------\n");
                printf("Task               State Prio Stack  Num  Aff    Ticks        %% Core\n");
                printf("--------------------------------------------------------------------------------\n");

                uint32_t u32TotalCoreUsageX100 = 0;

                for (UBaseType_t i = 0; i < uxTaskCount; i++) {
                    UBaseType_t tNum = pxTaskStatusArray[i].xTaskNumber;

                    // Compute delta execution microseconds spent in this window
                    uint32_t u32CurrentTaskRunTime = pxTaskStatusArray[i].ulRunTimeCounter;

                    // Safely find previous runtime in tracked history array without buffer overflow
                    uint32_t u32PreviousRunTime = 0;
                    int slot_found = -1;
                    int empty_slot = -1;

                    for (int h = 0; h < MAX_TRACKED_TASKS; h++) {
                        if (xTaskHistory[h].xTaskNumber == tNum) {
                            slot_found = h;
                            u32PreviousRunTime = xTaskHistory[h].ulLastRunTime;
                            break;
                        } else if (xTaskHistory[h].xTaskNumber == 0 && empty_slot == -1) {
                            empty_slot = h;
                        }
                    }

                    uint32_t u32DeltaTaskUs = u32CurrentTaskRunTime - u32PreviousRunTime;

                    // Update history slots
                    if (slot_found != -1) {
                        xTaskHistory[slot_found].ulLastRunTime = u32CurrentTaskRunTime;
                    } else if (empty_slot != -1) {
                        xTaskHistory[empty_slot].xTaskNumber = tNum;
                        xTaskHistory[empty_slot].ulLastRunTime = u32CurrentTaskRunTime;
                    }

                    // Calculate percentage * 100 (e.g. 21.15% stored as 2115)
                    uint32_t u32CorePctX100 = 0;
                    if (u64ElapsedUs > 0) {
                        u32CorePctX100 = (uint32_t)(((uint64_t)u32DeltaTaskUs * 10000ULL) / u64ElapsedUs);
                    }
                    u32TotalCoreUsageX100 += u32CorePctX100;

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

                    // Mask affinity to 8-bit hex representation
                    uint8_t u8Affinity = (uint8_t)(pxTaskStatusArray[i].uxCoreAffinityMask & 0xFF);

                    // Print formatted row (Exactly 80 chars total)
                    printf("%-16.16s  %c   %2lu  %5u %5lu 0x%02X %10lu  %3lu.%02lu%%\n", pxTaskStatusArray[i].pcTaskName, cState, pxTaskStatusArray[i].uxCurrentPriority, pxTaskStatusArray[i].usStackHighWaterMark, pxTaskStatusArray[i].xTaskNumber, u8Affinity, pxTaskStatusArray[i].ulRunTimeCounter, u32CorePctX100 / 100, u32CorePctX100 % 100);
                }

                printf("--------------------------------------------------------------------------------\n");
                printf("TOTAL DUAL-CORE LOAD (Max 200.00%%)                       : %3lu.%02lu%%\n", u32TotalCoreUsageX100 / 100, u32TotalCoreUsageX100 % 100);
                printf("================================================================================\n\n");

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
        configMINIMAL_STACK_SIZE + 512, // 512-word stack allocation to prevent printf overflow
        NULL,
        2, /* Low priority background task */
        NULL);
}

// vim:ts=4 et nowrap autoindent