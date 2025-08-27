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

#pragma once

#include <arch/info.h>
#include <kernel/com/spinlock.h>
#include <lib/util.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>

// NOTE: all of this reasonably asumes that KRADIXTREE_LAYER_SIZE be even
#define KRADIXTREE_LAYER_SIZE (ARCH_PAGE_SIZE / sizeof(void *))
#define KRADIXTREE_INDEX_BITS (__builtin_ffsll(KRADIXTREE_LAYER_SIZE) - 1)
#define KRADIXTREE_INDEX_MASK (KRADIXTREE_LAYER_SIZE - 1)
#define KRADIXTREE_MAX_LAYERS                                        \
    ((sizeof(uintmax_t) * CHAR_BIT - __builtin_clzll(UINTMAX_MAX)) / \
     KRADIXTREE_INDEX_BITS)

#define KRADIXTREE_INIT(rxtreeptr, num_layers)    \
    KASSERT(num_layers <= KRADIXTREE_MAX_LAYERS); \
    (rxtreeptr)->num_layers = num_layers;         \
    (rxtreeptr)->lock       = COM_SPINLOCK_NEW(); \
    (rxtreeptr)->back       = (void *)ARCH_PHYS_TO_HHDM(com_mm_pmm_alloc_zero())

// TODO: implement this
// NOTE: this frees the backing tree, not the whole structure
#define KRADIXTREE_FREE(rxtreeptr)

// Given its definition, sizeof(union kradixtree_back) == ARCH_PAGE_SIZE, thus
// this cannot contain radix tree metadata otehrwise it cannot be allocated with
// slab
typedef union kradixtree_back {
    union kradixtree_back *branches[KRADIXTREE_LAYER_SIZE];
    void                  *leaves[KRADIXTREE_LAYER_SIZE];
} kradixtree_back_t;

// This is needed to keep metadata as stated above
// NOTE: This contains a lock, but _nolock operations may be used under other
// locks, the important thing is to be consistent
typedef struct kradixtree {
    size_t             num_layers;
    kradixtree_back_t *back;
    com_spinlock_t     lock;
} kradixtree_t;

kradixtree_t *kradixtree_new(size_t num_layers);
int           kradixtree_free(kradixtree_t *rxtree);
int           kradixtree_get(void **out, kradixtree_t *rxtree, uintmax_t index);
int kradixtree_get_nolock(void **out, kradixtree_t *rxtree, uintmax_t index);
int kradixtree_put(kradixtree_t *rxtree, uintmax_t index, void *data);
int kradixtree_put_nolock(kradixtree_t *rxtree, uintmax_t index, void *data);
