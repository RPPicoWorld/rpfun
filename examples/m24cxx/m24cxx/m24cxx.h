/**
 ******************************************************************************
 * @file           : m24cxx.h
 * @brief          : M24Cxx Library Header
 ******************************************************************************
 * @attention
 *
 * Copyright (c) 2026
 * Lars Boegild Thomsen <lbthomsen@gmail.com>
 * All rights reserved.
 *
 * This software is licensed under terms that can be found in the LICENSE file
 * in the root directory of this software component.
 * If no LICENSE file comes with this software, it is provided AS-IS.
 *
 ******************************************************************************
 */

#ifndef M24CXX_H_
#define M24CXX_H_

#include "hardware/i2c.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define M24C08 0
#define M24M01 1
#define M24M01X4 2

#if M24CXX_MODEL == M24C08
#define M24CXX_TYPE "24C08"
#define M24CXX_SIZE 1024
#define M24CXX_ADDRESS_BITS 8
#define M24CXX_READ_PAGE_SIZE 256
#define M24CXX_WRITE_PAGE_SIZE 16
#define M24CXX_WRITE_TIMEOUT 100
#elif M24CXX_MODEL == M24M01
#define M24CXX_TYPE "24M01"
#define M24CXX_SIZE 131072
#define M24CXX_ADDRESS_BITS 16
#define M24CXX_READ_PAGE_SIZE 256
#define M24CXX_WRITE_PAGE_SIZE 256
#define M24CXX_WRITE_TIMEOUT 100
#elif M24CXX_MODEL == M24M01X4
#define M24CXX_TYPE "4 x 24M01"
#define M24CXX_SIZE 524288
#define M24CXX_ADDRESS_BITS 16
#define M24CXX_READ_PAGE_SIZE 256
#define M24CXX_WRITE_PAGE_SIZE 256
#define M24CXX_WRITE_TIMEOUT 100
#else
#error "M24CXX_MODEL must be defined in project properties"
#endif

#ifdef xxxDEBUG
#define m24cxx_dbg(...)  \
    printf(__VA_ARGS__); \
    printf("\r\n")
#else
#define m24cxx_dbg(...)
#endif

typedef struct {
    i2c_inst_t *i2c;
    uint8_t i2c_address;
    int wp_pin;
} m24cxx_t;

typedef enum {
    M24CXX_OK,
    M24CXX_ERR
} m24cxx_status_t;

m24cxx_status_t m24cxx_init(m24cxx_t *m24cxx, i2c_inst_t *i2c, uint8_t i2c_address, int wp_pin);
m24cxx_status_t m24cxx_is_connected(m24cxx_t *m24cxx);
m24cxx_status_t m24cxx_read(m24cxx_t *m24cxx, uint32_t address, uint8_t *data, uint32_t len);
m24cxx_status_t m24cxx_write(m24cxx_t *m24cxx, uint32_t address, uint8_t *data, uint32_t len);
m24cxx_status_t m24cxx_erase(m24cxx_t *m24cxx, uint32_t address, uint32_t len);
m24cxx_status_t m24cxx_erase_all(m24cxx_t *m24cxx);

#endif /* M24CXX_H_ */

// vim: ts=4 et nowrap