#pragma once

#include <kernel/com/fs/devfs.h>
#include <kernel/com/util.h>

// Registers the raw device in devfs as name, then each MBR or GPT partition on
// it as namepN. devops must implement read, write, get_page and stat. size is
// in bytes, lba_size is the logical block size partition tables are written in
int blockdev_register(const char *name, usize namelen, DevOps *devops,
                      void *devdata, u64 size, u32 lba_size);
