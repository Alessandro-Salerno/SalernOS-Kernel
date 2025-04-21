/*************************************************************************
| SalernOS Kernel                                                        |
| Copyright (C) 2021 - 2025 Alessandro Salerno                           |
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

#define INDEXMASK (COM_FS_PAGECACHE_LAYER_SIZE - 1)

static inline void split_index(uint64_t index, uint64_t indices[], size_t len) {
    for (size_t i = 0; i < len; i++) {
        indices[i] = (index >> (9 * (len - i - 1))) & INDEXMASK;
    }
}

bool com_fs_pagecache_get(uintptr_t       *out,
                          com_pagecache_t *root,
                          uint64_t         index) {
    uint64_t indices[4] = {0};
    split_index(index, indices, 4);

    for (size_t i = 0; i < 3; i++) {
        root = root->inner.next[indices[i]];

        if (NULL == root) {
            return false;
        }
    }

    uintptr_t data = root->leaf.data[indices[3]];

    if ((uintptr_t)NULL == data) {
        return false;
    }

    *out = data;
    return true;
}

void com_fs_pagecache_default(uintptr_t       *out,
                              com_pagecache_t *root,
                              uint64_t         index) {
    uint64_t indices[4] = {0};
    split_index(index, indices, 4);

    for (size_t i = 0; i < 3; i++) {
        com_pagecache_t *tmp = root->inner.next[indices[i]];

        if (NULL == tmp) {
            void *page = (void *)ARCH_PHYS_TO_HHDM(com_mm_pmm_alloc());
            kmemset(page, ARCH_PAGE_SIZE, 0);
            tmp = root->inner.next[indices[i]] = page;
        }

        root = tmp;
    }

    uintptr_t data = root->leaf.data[indices[3]];

    if ((uintptr_t)NULL == data) {
        void *page = (void *)ARCH_PHYS_TO_HHDM(com_mm_pmm_alloc());
        kmemset(page, ARCH_PAGE_SIZE, 0);
        data = root->leaf.data[indices[3]] = (uintptr_t)page;
    }

    *out = data;
}

com_pagecache_t *com_fs_pagecache_new(void) {
    void *phys = com_mm_pmm_alloc();

    if (NULL == phys) {
        return NULL;
    }

    com_pagecache_t *root = (void *)ARCH_PHYS_TO_HHDM(phys);
    kmemset(root, ARCH_PAGE_SIZE, 0);
    return root;
}
