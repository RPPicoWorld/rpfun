/**
 ******************************************************************************
 * @file           : m24cxx.c
 * @brief          : M24Cxx Library Source
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026 Lars Boegild Thomsen
 * Lars Boegild Thomsen <lbthomsen@gmail.com>
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */

#include "m24cxx.h"
#include "hardware/gpio.h"
#include "pico/error.h"
#include <string.h>

/* Internal Helper Functions */

// ACK Polling using your exact working implementation
static void eeprom_wait_for_write_complete(i2c_inst_t *i2c, uint8_t dev_addr) {
    uint8_t dummy_rx;
    int result;

    do {
        result = i2c_read_blocking(i2c, dev_addr, &dummy_rx, 1, false);
    } while (result == PICO_ERROR_GENERIC);
}

static void set_wp(m24cxx_t *m24cxx, bool enable) {
    if (m24cxx->wp_pin >= 0) {
        gpio_put(m24cxx->wp_pin, enable ? 1 : 0);
    }
}

static inline uint8_t get_dev_addr(m24cxx_t *m24cxx, uint32_t address) {
#if M24CXX_MODEL == M24C08
    return 0x50 | ((uint8_t)(address >> 8) & 0x03);
#elif M24CXX_MODEL == M24M01 || M24CXX_MODEL == M24M01X4
    // Base 0x50 + 64 KiB block index (A2, A1, A16)
    return 0x50 | ((uint8_t)(address >> 16) & 0x07);
#else
    return m24cxx->i2c_address + (uint8_t)(address >> M24CXX_ADDRESS_BITS);
#endif
}

/* Public Functions */

m24cxx_status_t m24cxx_init(m24cxx_t *m24cxx, i2c_inst_t *i2c, uint8_t i2c_address, int wp_pin) {
    m24cxx->i2c = i2c;
    m24cxx->i2c_address = i2c_address;
    m24cxx->wp_pin = wp_pin;

    if (m24cxx->wp_pin >= 0) {
        gpio_init(m24cxx->wp_pin);
        gpio_set_dir(m24cxx->wp_pin, GPIO_OUT);
        set_wp(m24cxx, false);
    }

    return m24cxx_is_connected(m24cxx);
}

m24cxx_status_t m24cxx_is_connected(m24cxx_t *m24cxx) {
    uint8_t dummy;
    uint8_t dev_addr = get_dev_addr(m24cxx, 0);
    if (i2c_read_blocking(m24cxx->i2c, dev_addr, &dummy, 1, false) == PICO_ERROR_GENERIC) {
        return M24CXX_ERR;
    }
    return M24CXX_OK;
}

m24cxx_status_t m24cxx_read(m24cxx_t *m24cxx, uint32_t address, uint8_t *data, uint32_t len) {
    uint32_t bytes_read = 0;

    while (bytes_read < len) {
        uint32_t curr_addr = address + bytes_read;
        uint32_t remaining = len - bytes_read;

        uint8_t dev_addr = get_dev_addr(m24cxx, curr_addr);

        // Max length per I2C read cycle capped to 64 KiB block boundary
        uint32_t space_in_block = 65536 - (curr_addr % 65536);
        uint32_t chunk_len = (remaining < space_in_block) ? remaining : space_in_block;

        uint16_t offset16 = (uint16_t)(curr_addr & 0xFFFF);
        uint8_t addr_bytes[2];
        addr_bytes[0] = (uint8_t)(offset16 >> 8);
        addr_bytes[1] = (uint8_t)(offset16 & 0xFF);

        // 1. Write the memory address we want to read from (nostop = true)
        int write_res = i2c_write_blocking(m24cxx->i2c, dev_addr, addr_bytes, 2, true);
        if (write_res == PICO_ERROR_GENERIC)
            return M24CXX_ERR;

        // 2. Read the data back from the device
        int read_res = i2c_read_blocking(m24cxx->i2c, dev_addr, data + bytes_read, chunk_len, false);
        if (read_res == PICO_ERROR_GENERIC)
            return M24CXX_ERR;

        bytes_read += chunk_len;
    }

    return M24CXX_OK;
}

m24cxx_status_t m24cxx_write(m24cxx_t *m24cxx, uint32_t address, uint8_t *data, uint32_t len) {
    uint32_t bytes_written = 0;

    set_wp(m24cxx, false);

    while (bytes_written < len) {
        uint32_t curr_addr = address + bytes_written;
        uint32_t remaining = len - bytes_written;

        uint32_t page_offset = curr_addr % M24CXX_WRITE_PAGE_SIZE;
        uint32_t space_in_page = M24CXX_WRITE_PAGE_SIZE - page_offset;
        uint32_t chunk_len = (remaining < space_in_page) ? remaining : space_in_page;

        uint8_t dev_addr = get_dev_addr(m24cxx, curr_addr);

        uint8_t buffer[256 + 2];
        uint16_t offset16 = (uint16_t)(curr_addr & 0xFFFF);

        buffer[0] = (uint8_t)(offset16 >> 8);
        buffer[1] = (uint8_t)(offset16 & 0xFF);

        for (size_t i = 0; i < chunk_len; i++) {
            buffer[i + 2] = data[bytes_written + i];
        }

        int result = i2c_write_blocking(m24cxx->i2c, dev_addr, buffer, chunk_len + 2, false);
        if (result == PICO_ERROR_GENERIC) {
            set_wp(m24cxx, true);
            return M24CXX_ERR;
        }

        // ACK polling after page write
        eeprom_wait_for_write_complete(m24cxx->i2c, dev_addr);

        bytes_written += chunk_len;
    }

    set_wp(m24cxx, true);
    return M24CXX_OK;
}

m24cxx_status_t m24cxx_erase(m24cxx_t *m24cxx, uint32_t address, uint32_t len) {
    uint8_t buf[M24CXX_WRITE_PAGE_SIZE];
    memset(buf, 0xff, sizeof(buf));

    uint32_t written = 0;
    while (written < len) {
        uint32_t chunk = (len - written > sizeof(buf)) ? sizeof(buf) : (len - written);
        m24cxx_status_t result = m24cxx_write(m24cxx, address + written, buf, chunk);
        if (result != M24CXX_OK)
            return result;
        written += chunk;
    }

    return M24CXX_OK;
}

m24cxx_status_t m24cxx_erase_all(m24cxx_t *m24cxx) {
    return m24cxx_erase(m24cxx, 0, M24CXX_SIZE);
}