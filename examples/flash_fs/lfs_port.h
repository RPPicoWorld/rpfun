/**
 * @file lfs_port.h
 * @brief LittleFS port for RP2350 flash.
 * @author STM32World <lth@stm32world.com>
 * @date 2026
 *
 * Copyright (c) 2026 STM32World <lth@stm32world.com>
 *
 * Connects LittleFS to the part of the on-board QSPI flash reserved for the filesystem
 * by the linker script (linker_overrides/memory_flash.incl).
 */

#ifndef LFS_PORT_H
#define LFS_PORT_H

#include "littlefs/lfs.h" // Include the LittleFS header for filesystem operations

/**
 * @brief Mounts the LittleFS filesystem in the flash FS region.
 *
 * The filesystem is not formatted here: the FS region must already hold a LittleFS image,
 * e.g. the littlefs.bin built from fs_root/ and flashed at the start of the FS region.
 *
 * @param lfs Pointer to the LittleFS instance to mount.
 * @return int 0 (LFS_ERR_OK) on success, or a negative LittleFS error code.
 */
int lfs_port_init(lfs_t *lfs);

#endif // LFS_PORT_H
