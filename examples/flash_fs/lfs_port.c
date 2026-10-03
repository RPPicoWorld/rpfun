/**
 * @file lfs_port.c
 * @brief LittleFS port for RP2350 flash.
 */

#include "lfs_port.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"
#include <string.h>

// Import symbols exported directly from the linker script (memory_flash.incl)
extern char __flash_fs_offset[];
extern char __flash_fs_length[];

// Calculate address offsets and total flash size from linker symbols
#define FS_FLASH_OFFSET ((uint32_t)(uintptr_t)__flash_fs_offset)
#define FS_FLASH_SIZE ((uint32_t)(uintptr_t)__flash_fs_length)

#define FS_BLOCK_SIZE 4096
#define FS_READ_SIZE 256
#define FS_PROG_SIZE 256

static int lfs_pico_read(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, void *buffer, lfs_size_t size) {
    uint32_t flash_addr = XIP_BASE + FS_FLASH_OFFSET + (block * c->block_size) + off;
    memcpy(buffer, (const void *)flash_addr, size);
    return LFS_ERR_OK;
}

static int lfs_pico_prog(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, const void *buffer, lfs_size_t size) {
    uint32_t flash_offset = FS_FLASH_OFFSET + (block * c->block_size) + off;
    uint32_t ints = save_and_disable_interrupts();
    flash_range_program(flash_offset, (const uint8_t *)buffer, size);
    restore_interrupts(ints);
    return LFS_ERR_OK;
}

static int lfs_pico_erase(const struct lfs_config *c, lfs_block_t block) {
    uint32_t flash_offset = FS_FLASH_OFFSET + (block * c->block_size);
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(flash_offset, c->block_size);
    restore_interrupts(ints);
    return LFS_ERR_OK;
}

static int lfs_pico_sync(const struct lfs_config *c) {
    (void)c;
    return LFS_ERR_OK;
}

static struct lfs_config cfg = {
    .read = lfs_pico_read,
    .prog = lfs_pico_prog,
    .erase = lfs_pico_erase,
    .sync = lfs_pico_sync,

    .read_size = FS_READ_SIZE,
    .prog_size = FS_PROG_SIZE,
    .block_size = FS_BLOCK_SIZE,
    .block_cycles = 500,
};

int lfs_port_init(lfs_t *lfs) {
    // Derive block count dynamically from linker script length symbol
    cfg.block_count = FS_FLASH_SIZE / FS_BLOCK_SIZE;
    cfg.cache_size = FS_READ_SIZE;
    cfg.lookahead_size = 32;

    return lfs_mount(lfs, &cfg);
}
