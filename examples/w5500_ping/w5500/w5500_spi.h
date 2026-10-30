#ifndef W5500_SPI_H
#define W5500_SPI_H

#include "hardware/spi.h"
#include "pico/stdlib.h"
#include "wizchip_conf.h"

// Hardware SPI assignment for RP2354
#define W5500_SPI_PORT spi1
#define W5500_SPI_BAUD (16 * 1000 * 1000) // 16 MHz

// Pin assignments
#define PIN_W5500_CS 27   // Software controlled Chip Select
#define PIN_W5500_SCK 42  // SPI1 SCK
#define PIN_W5500_MOSI 43 // SPI1 TX
#define PIN_W5500_MISO 40 // SPI1 RX
#define PIN_W5500_INT 10  // Interrupt Pin
#define PIN_W5500_RST 11  // Hardware Reset Pin

int W5500_Init(wiz_NetInfo *pnetinfo);

#endif // W5500_SPI_H