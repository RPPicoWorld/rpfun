#ifndef _BOARDS_SL_MCU_RP2354_H
#define _BOARDS_SL_MCU_RP2354_H

pico_board_cmake_set(PICO_PLATFORM, rp2350)

#define SL_MCU_RP2354

// --- RP2350 VARIANT ---
#define PICO_RP2350B 1

#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (2 * 1024 * 1024)
#endif

// Based on schematic: GPIO 31
#ifndef PICO_DEFAULT_LED_PIN
#define PICO_DEFAULT_LED_PIN 31
#endif

// --- UART ---
#ifndef PICO_DEFAULT_UART
#define PICO_DEFAULT_UART 0
#endif
#ifndef PICO_DEFAULT_UART_TX_PIN
#define PICO_DEFAULT_UART_TX_PIN 0
#endif
#ifndef PICO_DEFAULT_UART_RX_PIN
#define PICO_DEFAULT_UART_RX_PIN 1
#endif

#endif // _BOARDS_SL_MCU_RP2354_H