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
#include <kernel/com/io/log.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/spinlock.h>
#include <lib/mem.h>
#include <stddef.h>
#include <stdint.h>

#include "lib/util.h"

#define NUM_SLABS (ARCH_PAGE_SIZE / 16)

typedef struct {
    uintptr_t next;
} slab_t;

static slab_t         Slabs[NUM_SLABS] = {0};
static com_spinlock_t Lock             = COM_SPINLOCK_NEW();

static void init(slab_t *s, size_t entry_size) {
    entry_size     = (entry_size + 15) & ~15UL;
    void *new_page = com_mm_pmm_alloc();
    s->next        = ARCH_PHYS_TO_HHDM(new_page);
    size_t max     = ARCH_PAGE_SIZE / entry_size - 1;
#ifndef COM_MM_PMM_ZERO_POLICY
    kmemset((void *)s->next, ARCH_PAGE_SIZE, 0);
#endif
    uintptr_t *list_arr = (uintptr_t *)s->next;
    size_t     off      = entry_size / sizeof(uintptr_t);

    for (size_t i = 0; i < max; i++) {
        list_arr[i * off] = (uintptr_t)&list_arr[(i + 1) * off];
    }

    list_arr[max * off] = (uintptr_t)NULL;
}

void *com_mm_slab_alloc(size_t size) {
    size_t i = (size + 15) / 16;
    KASSERT(i < NUM_SLABS);
    slab_t *s = &Slabs[i];

    com_spinlock_acquire(&Lock);
    if (0 == s->next) {
        init(s, size);
    }
    uintptr_t *old_next = (uintptr_t *)s->next;
    s->next             = *old_next;
    com_spinlock_release(&Lock);

#if !defined(COM_MM_PMM_ZERO_POLICY) || \
    COM_MM_PMM_ZERO_ON_ALLOC & COM_MM_PMM_ZERO_POLICY
    kmemset(old_next, size, 0);
#elif defined(COM_MM_PMM_ZERO_POLICY) && \
    COM_MM_PMM_ZERO_ON_FREE & COM_MM_PMM_ZERO_POLICY
    *old_next = 0;
#endif
    return old_next;
}

void com_mm_slab_free(void *ptr, size_t size) {
    if (NULL == ptr) {
        return;
    }

    size_t i = (size + 15) / 16;
    KASSERT(i < NUM_SLABS);
    slab_t *s = &Slabs[i];

    com_spinlock_acquire(&Lock);
    uintptr_t *new_head = ptr;
#if defined(COM_MM_PMM_ZERO_POLICY) && \
    COM_MM_PMM_ZERO_ON_FREE & COM_MM_PMM_ZERO_POLICY
    kmemset(new_head, size, 0);
#endif
    *new_head = s->next;
    s->next   = (uintptr_t)new_head;
    com_spinlock_release(&Lock);

    // TODO: free slab page if needed
}
