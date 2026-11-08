/*************************************************************************
| SalernOS Kernel                                                        |
| Copyright (C) 2021 - 2026 Alessandro Salerno                           |
|                                                                        |
| This program is free software: you can redistribute it and/or modify   |
| it under the terms of the GNU General Public License as published by   |
| the Free Software Foundation, either version 3 of the License, or      |
| (at your option) any later version.                                    |
|                                                                        |
| This program is distributed in the hope that it will be useful,        |
| but WITHOUT ANY WARRANTY; without even the implied warranty of         |
| MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          |
| GNU General Public License for more details.                           |
|                                                                        |
| You should have received a copy of the GNU General Public License      |
| along with this program.  If not, see <https://www.gnu.org/licenses/>. |
*************************************************************************/

#include <arch/info.h>
#include <kernel/com/fs/pagecache.h>
#include <kernel/com/mm/pmm.h>
#include <lib/mem.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <strings.h>

// This is all temporary while I rewrite VFS

bool com_fs_pagecache_get(uintptr_t       *out,
                          com_pagecache_t *root,
                          uint64_t         index) {
    void *out_void;
    if (0 != kradixtree_get_nolock(&out_void, root, index)) {
        return false;
    }
    *out = (uintptr_t)out_void;
    return true;
}

void com_fs_pagecache_default(uintptr_t       *out,
                              com_pagecache_t *root,
                              uint64_t         index) {
    if (com_fs_pagecache_get(out, root, index)) {
        return;
    }

    void *data = (void *)ARCH_PHYS_TO_HHDM(com_mm_pmm_alloc_zero());
    kradixtree_put_nolock(root, index, data);
    *out = (uintptr_t)data;
}

com_pagecache_t *com_fs_pagecache_new(void) {
    return kradixtree_new(COM_FS_PAGECACHE_LAYERS);
}

static void free_layer(kradixtree_back_t *node, size_t layers_left) {
    for (size_t i = 0; i < KRADIXTREE_LAYER_SIZE; i++) {
        if (layers_left > 1 && NULL != node->branches[i]) {
            free_layer(node->branches[i], layers_left - 1);
        } else if (1 == layers_left && NULL != node->leaves[i]) {
            com_mm_pmm_free((void *)ARCH_HHDM_TO_PHYS(node->leaves[i]));
        }
    }

    com_mm_pmm_free((void *)ARCH_HHDM_TO_PHYS(node));
}

void com_fs_pagecache_free(com_pagecache_t *root) {
    free_layer(root->back, root->num_layers);
    kradixtree_free(root);
}
