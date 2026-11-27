/**
 * @file lfs_port.c
 * @brief LittleFS port for RP2350 flash.
 * @author STM32World <lth@stm32world.com>
 * @date 2026
 *
 * Copyright (c) 2026 STM32World <lth@stm32world.com>
 *
 * Implements the LittleFS block device callbacks (read, prog, erase, sync) on top of the
 * Pico SDK flash functions. The filesystem lives in the FLASH_FS region defined in the linker
 * script, after the 1 MB reserved for the program.
 *
 * Reads go straight through the memory-mapped XIP window. Erase and program use the SDK, with
 * the flash read (XIP) setup saved and restored around each call so they also work when the
 * RP2350 is overclocked (see flash_erase_keep_xip() below).
 */

#include "lfs_port.h"
#include "hardware/flash.h"       // flash_range_erase(), flash_range_program(), flash_flush_cache()
#include "hardware/structs/qmi.h" // QMI (flash interface) registers
#include "hardware/sync.h"        // save_and_disable_interrupts()
#include "pico/stdlib.h"
#include <string.h>

// Import symbols exported directly from the linker script (memory_flash.incl)
extern char __flash_fs_offset[]; // Start of the FS region, as an offset from the start of flash
extern char __flash_fs_length[]; // Size of the FS region in bytes

// The linker symbols carry their value in their address, so convert the addresses to numbers
#define FS_FLASH_OFFSET ((uint32_t)(uintptr_t)__flash_fs_offset)
#define FS_FLASH_SIZE ((uint32_t)(uintptr_t)__flash_fs_length)

#define FS_BLOCK_SIZE 4096 // LittleFS block = one flash sector (the smallest erasable unit)
#define FS_READ_SIZE 256   // Minimum read size, one flash page
#define FS_PROG_SIZE 256   // Minimum program size, one flash page (flash_range_program() works in whole pages)

/**
 * @brief LittleFS read callback: copies data out of the memory-mapped flash.
 *
 * @param c LittleFS configuration (gives the block size).
 * @param block Block number within the FS region.
 * @param off Byte offset within the block.
 * @param buffer Destination buffer.
 * @param size Number of bytes to read.
 * @return int Always LFS_ERR_OK.
 */
static int lfs_pico_read(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, void *buffer, lfs_size_t size) {
    uint32_t flash_addr = XIP_BASE + FS_FLASH_OFFSET + (block * c->block_size) + off;
    memcpy(buffer, (const void *)flash_addr, size);
    return LFS_ERR_OK;
}

/*
 * Keeping the flash read (XIP) setup across erase/program
 * --------------------------------------------------------
 * The SDK's flash_range_erase() and flash_range_program() hand the flash over to the bootrom,
 * which does the erase/program at a safe clk_sys / 12. When they finish they put the flash
 * interface (QMI window 0) back to the boot time XIP setup: quad read with a fixed clock
 * divider of 3. That is fine at 150 MHz (50 MHz flash clock), but when overclocked it runs the
 * flash far too fast (180 MHz at a 540 MHz system clock) and the next instruction fetch from
 * flash crashes.
 *
 * So the wrappers below save the XIP setup before each operation and put it back afterwards.
 * This only works because the boot setup doesn't leave the flash in continuous read mode (every
 * read sends the command byte), so whatever read mode was in use can be restored afterwards.
 *
 * The wrappers must run from RAM, as flash can't be read while it is being erased/programmed,
 * and interrupts are disabled so no interrupt handler tries to run from flash in the meantime.
 */

/**
 * @brief The QMI window 0 registers that make up the XIP read setup.
 */
typedef struct {
    uint32_t timing; // Clock divider, RX delay and chip select timing
    uint32_t rfmt;   // Read format: bus widths and lengths of the command, address, dummy and data phases
    uint32_t rcmd;   // Read command byte (e.g. 0x03 serial read or 0xeb quad read)
} xip_setup_t;

/**
 * @brief Saves the current XIP read setup.
 * @param setup Where to store the register values.
 */
static __force_inline void save_xip_setup(xip_setup_t *setup) {
    setup->timing = qmi_hw->m[0].timing;
    setup->rfmt = qmi_hw->m[0].rfmt;
    setup->rcmd = qmi_hw->m[0].rcmd;
}

/**
 * @brief Restores a saved XIP read setup and flushes the XIP cache.
 * @param setup Register values saved by save_xip_setup().
 */
static __force_inline void restore_xip_setup(const xip_setup_t *setup) {
    qmi_hw->m[0].timing = setup->timing;
    qmi_hw->m[0].rfmt = setup->rfmt;
    qmi_hw->m[0].rcmd = setup->rcmd;
    flash_flush_cache(); // Drop anything cached while the old setup was active
}

/**
 * @brief Erases flash sectors without losing the current XIP read setup.
 *
 * @param flash_offset Offset from the start of flash, must be sector (4096 byte) aligned.
 * @param count Number of bytes to erase, must be a multiple of the sector size.
 */
static void __no_inline_not_in_flash_func(flash_erase_keep_xip)(uint32_t flash_offset, size_t count) {
    xip_setup_t setup;
    uint32_t ints = save_and_disable_interrupts(); // No interrupt handler may run from flash during the erase
    save_xip_setup(&setup);
    flash_range_erase(flash_offset, count);
    restore_xip_setup(&setup); // Undo the SDK's switch back to the boot time XIP setup
    restore_interrupts(ints);
}

/**
 * @brief Programs flash pages without losing the current XIP read setup.
 *
 * @param flash_offset Offset from the start of flash, must be page (256 byte) aligned.
 * @param data Data to write (must not be in flash).
 * @param count Number of bytes to write, must be a multiple of the page size.
 */
static void __no_inline_not_in_flash_func(flash_program_keep_xip)(uint32_t flash_offset, const uint8_t *data, size_t count) {
    xip_setup_t setup;
    uint32_t ints = save_and_disable_interrupts(); // No interrupt handler may run from flash while programming
    save_xip_setup(&setup);
    flash_range_program(flash_offset, data, count);
    restore_xip_setup(&setup); // Undo the SDK's switch back to the boot time XIP setup
    restore_interrupts(ints);
}

/**
 * @brief LittleFS program callback: writes data to an already erased part of a block.
 *
 * @param c LittleFS configuration (gives the block size).
 * @param block Block number within the FS region.
 * @param off Byte offset within the block (a multiple of FS_PROG_SIZE).
 * @param buffer Data to write.
 * @param size Number of bytes to write (a multiple of FS_PROG_SIZE).
 * @return int Always LFS_ERR_OK.
 */
static int lfs_pico_prog(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, const void *buffer, lfs_size_t size) {
    uint32_t flash_offset = FS_FLASH_OFFSET + (block * c->block_size) + off;
    flash_program_keep_xip(flash_offset, (const uint8_t *)buffer, size);
    return LFS_ERR_OK;
}

/**
 * @brief LittleFS erase callback: erases one block (one flash sector).
 *
 * @param c LittleFS configuration (gives the block size).
 * @param block Block number within the FS region.
 * @return int Always LFS_ERR_OK.
 */
static int lfs_pico_erase(const struct lfs_config *c, lfs_block_t block) {
    uint32_t flash_offset = FS_FLASH_OFFSET + (block * c->block_size);
    flash_erase_keep_xip(flash_offset, c->block_size);
    return LFS_ERR_OK;
}

/**
 * @brief LittleFS sync callback: nothing to do, as writes go straight to flash.
 *
 * @param c LittleFS configuration (unused).
 * @return int Always LFS_ERR_OK.
 */
static int lfs_pico_sync(const struct lfs_config *c) {
    (void)c;
    return LFS_ERR_OK;
}

// LittleFS configuration; block_count, cache_size and lookahead_size are filled in by lfs_port_init()
static struct lfs_config cfg = {
    .read = lfs_pico_read,
    .prog = lfs_pico_prog,
    .erase = lfs_pico_erase,
    .sync = lfs_pico_sync,

    .read_size = FS_READ_SIZE,
    .prog_size = FS_PROG_SIZE,
    .block_size = FS_BLOCK_SIZE,
    .block_cycles = 500, // Erase cycles before LittleFS moves metadata to another block (wear levelling)
};

int lfs_port_init(lfs_t *lfs) {
    // Derive block count dynamically from linker script length symbol
    cfg.block_count = FS_FLASH_SIZE / FS_BLOCK_SIZE;
    cfg.cache_size = FS_READ_SIZE; // One page of read/program cache
    cfg.lookahead_size = 32;       // 32 bytes of lookahead bitmap = 256 blocks searched per allocation pass

    return lfs_mount(lfs, &cfg);
}
