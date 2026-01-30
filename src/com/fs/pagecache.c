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
    return kradixtree_new(4);
}
