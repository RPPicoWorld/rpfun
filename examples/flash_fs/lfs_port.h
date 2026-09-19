#ifndef LFS_PORT_H
#define LFS_PORT_H

#include "lfs.h"

// 1MB Flash Offset for LittleFS Partition on 2MB Flash
#define LFS_FLASH_OFFSET (1024 * 1024)
#define LFS_FLASH_SIZE (1024 * 1024)

int lfs_port_init(lfs_t *lfs);

#endif