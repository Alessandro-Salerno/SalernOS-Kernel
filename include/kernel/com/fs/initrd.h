#pragma once

#include <kernel/com/fs/vfs.h>
#include <kernel/com/util.h>

void initrd_make(Vnode *root, void *tar, usize tarsize);
