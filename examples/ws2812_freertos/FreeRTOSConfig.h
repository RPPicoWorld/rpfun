#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include <assert.h>
#include <stdint.h>

/* Forward declare time_us_64 to avoid illegal Pico SDK header includes inside config */
extern uint64_t time_us_64(void);

/* --- SMP & Core Settings --- */
#define configNUMBER_OF_CORES 2
#define configRUN_MULTIPLE_PRIORITIES 1
#define configUSE_CORE_AFFINITY 1
#define configUSE_PASSIVE_IDLE_HOOK 0

/* --- Basic Scheduler Settings --- */
#define configUSE_PREEMPTION 1
#define configUSE_TIME_SLICING 1
#define configUSE_IDLE_HOOK 0
#define configUSE_TICK_HOOK 0
#define configTICK_RATE_HZ ((TickType_t)1000)
#define configMAX_PRIORITIES (5)
#define configMINIMAL_STACK_SIZE ((unsigned short)256)
#define configMAX_TASK_NAME_LEN (16)
#define configUSE_16_BIT_TICKS 0
#define configIDLE_SHOULD_YIELD 1

/* --- Mutex Configuration --- */
#define configUSE_MUTEXES 1
#define configUSE_RECURSIVE_MUTEXES 1

/* --- Software Timer Configuration --- */
#define configUSE_TIMERS 1
#define configTIMER_TASK_PRIORITY (configMAX_PRIORITIES - 1)
#define configTIMER_QUEUE_LENGTH 10
#define configTIMER_TASK_STACK_DEPTH 1024
#define INCLUDE_xTimerPendFunctionCall 1

/* --- Task Statistics & Runtime Tracking --- */
#define configUSE_TRACE_FACILITY 1
#define configUSE_STATS_FORMATTING_FUNCTIONS 1
#define configGENERATE_RUN_TIME_STATS 1

/* Use Pico SDK hardware timer (microseconds) for run-time statistics counter */
#define portCONFIGURE_TIMER_FOR_RUN_TIME_STATS() /* Hardware timer is active on boot */
#define portGET_RUN_TIME_COUNTER_VALUE() ((uint32_t)time_us_64())

/* --- Assert Definition --- */
#define configASSERT(x) assert(x)

/* --- Memory Allocation --- */
#define configSUPPORT_DYNAMIC_ALLOCATION 1
#define configSUPPORT_STATIC_ALLOCATION 0
#define configTOTAL_HEAP_SIZE ((size_t)(64 * 1024))

/* --- Pico SDK Hardware Interoperability --- */
#define configSUPPORT_PICO_SYNC_INTEROP 1
#define configSUPPORT_PICO_TIME_INTEROP 1

/* --- RP2350 / ARM Cortex-M33 Specifics --- */
#define configENABLE_FPU 1
#define configENABLE_MPU 0
#define configENABLE_TRUSTZONE 0
#define configRUN_FREERTOS_SECURE_ONLY 1
#define configMAX_SYSCALL_INTERRUPT_PRIORITY 16

/* --- API Functions Inclusion --- */
#define INCLUDE_vTaskPrioritySet 1
#define INCLUDE_uxTaskPriorityGet 1
#define INCLUDE_vTaskDelete 1
#define INCLUDE_vTaskSuspend 1
#define INCLUDE_vTaskDelayUntil 1
#define INCLUDE_vTaskDelay 1
#define INCLUDE_xTaskGetSchedulerState 1
#define INCLUDE_xTaskGetCurrentTaskHandle 1

#endif /* FREERTOS_CONFIG_H */