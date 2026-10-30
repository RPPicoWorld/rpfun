#include "w5500_spi.h"
#include <stdio.h>

// -----------------------------------------------------------------------------
// Chip Select & SPI Callbacks
// -----------------------------------------------------------------------------
static inline void w5500_cs_select(void) {
    asm volatile("nop \n nop \n nop");
    gpio_put(PIN_W5500_CS, 0);
    asm volatile("nop \n nop \n nop");
}

static inline void w5500_cs_deselect(void) {
    asm volatile("nop \n nop \n nop");
    gpio_put(PIN_W5500_CS, 1);
    asm volatile("nop \n nop \n nop");
}

static uint8_t w5500_spi_read_byte(void) {
    uint8_t rx_data = 0;
    uint8_t dummy = 0xFF;
    spi_read_blocking(W5500_SPI_PORT, dummy, &rx_data, 1);
    return rx_data;
}

static void w5500_spi_write_byte(uint8_t tx_data) {
    spi_write_blocking(W5500_SPI_PORT, &tx_data, 1);
}

static void w5500_spi_read_burst(uint8_t *pBuf, uint16_t len) {
    spi_read_blocking(W5500_SPI_PORT, 0xFF, pBuf, len);
}

static void w5500_spi_write_burst(uint8_t *pBuf, uint16_t len) {
    spi_write_blocking(W5500_SPI_PORT, pBuf, len);
}

// -----------------------------------------------------------------------------
// Hardware Reset Sequence
// -----------------------------------------------------------------------------
static void w5500_hw_reset(void) {
    gpio_put(PIN_W5500_RST, 0);
    sleep_ms(10);
    gpio_put(PIN_W5500_RST, 1);
    sleep_ms(50);
}

// -----------------------------------------------------------------------------
// W5500 Driver Initialization
// -----------------------------------------------------------------------------
int W5500_Init(wiz_NetInfo *pnetinfo) {
    // 1. Initialize GPIO pins
    gpio_init(PIN_W5500_CS);
    gpio_set_dir(PIN_W5500_CS, GPIO_OUT);
    gpio_put(PIN_W5500_CS, 1);

    gpio_init(PIN_W5500_RST);
    gpio_set_dir(PIN_W5500_RST, GPIO_OUT);
    gpio_put(PIN_W5500_RST, 1);

    gpio_init(PIN_W5500_INT);
    gpio_set_dir(PIN_W5500_INT, GPIO_IN);
    gpio_pull_up(PIN_W5500_INT);

    // 2. Configure SPI peripheral pins
    spi_init(W5500_SPI_PORT, W5500_SPI_BAUD);
    gpio_set_function(PIN_W5500_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_W5500_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(PIN_W5500_MISO, GPIO_FUNC_SPI);

    // 3. Hardware Reset W5500
    w5500_hw_reset();

    // 4. Register WIZnet driver callbacks
    reg_wizchip_cris_cbfunc(NULL, NULL);
    reg_wizchip_cs_cbfunc(w5500_cs_select, w5500_cs_deselect);
    reg_wizchip_spi_cbfunc(w5500_spi_read_byte, w5500_spi_write_byte);
    reg_wizchip_spiburst_cbfunc(w5500_spi_read_burst, w5500_spi_write_burst);

    // 5. Buffer size initialization (2KB per socket for 8 sockets)
    uint8_t memsize[2][8] = {
        { 2, 2, 2, 2, 2, 2, 2, 2 },
        { 2, 2, 2, 2, 2, 2, 2, 2 }
    };

    if (wizchip_init(memsize[0], memsize[1]) != 0) {
        return -1;
    }

    // 6. Set initial MAC and network parameters
    if (pnetinfo != NULL) {
        wizchip_setnetinfo(pnetinfo);
    }

    return 0;
}