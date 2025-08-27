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

#include <errno.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/mm/slab.h>
#include <kernel/com/spinlock.h>
#include <lib/radixtree.h>

static inline void
split_index(uintmax_t indices[], uintmax_t index, size_t num_layers) {
    for (size_t i = 0; i < num_layers; i++) {
        indices[i] = (index >> (KRADIXTREE_INDEX_BITS * (num_layers - i - 1))) &
                     KRADIXTREE_INDEX_MASK;
    }
}

kradixtree_t *kradixtree_new(size_t num_layers) {
    kradixtree_t *rxtree = com_mm_slab_alloc(sizeof(kradixtree_t));
    KRADIXTREE_INIT(rxtree, num_layers);
    return rxtree;
}

int kradixtree_free(kradixtree_t *rxtree) {
    KRADIXTREE_FREE(rxtree);
    com_mm_slab_free(rxtree, sizeof(kradixtree_t));
    return 0;
}

int kradixtree_get(void **out, kradixtree_t *rxtree, uintmax_t index) {
    com_spinlock_acquire(&rxtree->lock);
    int ret = kradixtree_get_nolock(out, rxtree, index);
    com_spinlock_release(&rxtree->lock);
    return ret;
}

int kradixtree_get_nolock(void **out, kradixtree_t *rxtree, uintmax_t index) {
    uintmax_t indices[KRADIXTREE_MAX_LAYERS] = {0};
    split_index(indices, index, rxtree->num_layers);
    kradixtree_back_t *root       = rxtree->back;
    size_t             last_layer = rxtree->num_layers - 1;

    for (size_t i = 0; i < last_layer; i++) {
        root = root->branches[indices[i]];
        if (NULL == root) {
            return ENOENT;
        }
    }

    void *data = root->leaves[indices[last_layer]];
    if (NULL == data) {
        return ENOENT;
    }

    *out = data;
    return 0;
}

int kradixtree_put(kradixtree_t *rxtree, uintmax_t index, void *data) {
    com_spinlock_acquire(&rxtree->lock);
    int ret = kradixtree_put_nolock(rxtree, index, data);
    com_spinlock_release(&rxtree->lock);
    return ret;
}

int kradixtree_put_nolock(kradixtree_t *rxtree, uintmax_t index, void *data) {
    uintmax_t indices[KRADIXTREE_MAX_LAYERS] = {0};
    split_index(indices, index, rxtree->num_layers);
    kradixtree_back_t *root       = rxtree->back;
    size_t             last_layer = rxtree->num_layers - 1;

    for (size_t i = 0; i < last_layer; i++) {
        kradixtree_back_t *tmp = root->branches[indices[i]];

        if (NULL == tmp) {
            void *page = (void *)ARCH_PHYS_TO_HHDM(com_mm_pmm_alloc_zero());
            tmp = root->branches[indices[i]] = page;
        }

        root = tmp;
    }

    KASSERT(NULL == root->leaves[indices[last_layer]]);
    root->leaves[indices[last_layer]] = data;
    return 0;
}
