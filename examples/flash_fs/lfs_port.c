#include "lfs_port.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include <string.h>

static struct lfs_config cfg;

static int pico_read(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, void *buffer, lfs_size_t size) {
    uint32_t flash_addr = XIP_BASE + LFS_FLASH_OFFSET + (block * c->block_size) + off;
    memcpy(buffer, (void *)flash_addr, size);
    return LFS_ERR_OK;
}

static int pico_prog(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, const void *buffer, lfs_size_t size) {
    uint32_t flash_offset = LFS_FLASH_OFFSET + (block * c->block_size) + off;
    uint32_t ints = save_and_disable_interrupts();
    flash_range_program(flash_offset, buffer, size);
    restore_interrupts(ints);
    return LFS_ERR_OK;
}

static int pico_erase(const struct lfs_config *c, lfs_block_t block) {
    uint32_t flash_offset = LFS_FLASH_OFFSET + (block * c->block_size);
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(flash_offset, c->block_size);
    restore_interrupts(ints);
    return LFS_ERR_OK;
}

static int pico_sync(const struct lfs_config *c) {
    return LFS_ERR_OK;
}

int lfs_port_init(lfs_t *lfs) {
    cfg.read = pico_read;
    cfg.prog = pico_prog;
    cfg.erase = pico_erase;
    cfg.sync = pico_sync;

    cfg.read_size = 16;
    cfg.prog_size = FLASH_PAGE_SIZE;                      // 256 B
    cfg.block_size = FLASH_SECTOR_SIZE;                   // 4096 B
    cfg.block_count = LFS_FLASH_SIZE / FLASH_SECTOR_SIZE; // 256 blocks (1MB)
    cfg.cache_size = FLASH_PAGE_SIZE;
    cfg.lookahead_size = 32;
    cfg.block_cycles = 500;

    int err = lfs_mount(lfs, &cfg);
    if (err) {
        lfs_format(lfs, &cfg);
        err = lfs_mount(lfs, &cfg);
    }
    return err;
}