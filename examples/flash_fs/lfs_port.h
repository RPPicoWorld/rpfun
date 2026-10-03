/**
 * @file lfs_port.h
 * @brief LittleFS port for RP2350 flash.
 */

#ifndef LFS_PORT_H
#define LFS_PORT_H

#include "littlefs/lfs.h" // Include the LittleFS header for filesystem operations

int lfs_port_init(lfs_t *lfs);

#endif // LFS_PORT_H