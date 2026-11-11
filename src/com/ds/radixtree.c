#include <errno.h>
#include <kernel/com/ds/radixtree.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/mm/slab.h>
#include <kernel/com/sync/spinlock.h>
#include <kernel/com/util.h>

static inline void split_index(u64 indices[], u64 index, usize num_layers) {
    for (usize i = 0; i < num_layers; i++) {
        indices[i] = (index >> (RADIXTREE_INDEX_BITS * (num_layers - i - 1))) &
            RADIXTREE_INDEX_MASK;
    }
}

RadixTree *radixtree_new(usize num_layers) {
    RadixTree *rxtree = slab_alloc(sizeof(RadixTree));
    radixtree_init(rxtree, num_layers);
    return rxtree;
}

int radixtree_free(RadixTree *rxtree) {
    slab_free(rxtree, sizeof(RadixTree));
    return 0;
}

int radixtree_get(RadixTree *rxtree, u64 index, void **out_ptr) {
    spinlock_acq(&rxtree->lock);
    int ret = radixtree_get_nolock(rxtree, index, out_ptr);
    spinlock_rel(&rxtree->lock);
    return ret;
}

int radixtree_get_nolock(RadixTree *rxtree, u64 index, void **out_ptr) {
    u64 indices[RADIXTREE_MAX_LAYERS] = {0};
    split_index(indices, index, rxtree->num_layers);
    RadixTreeBack *root = rxtree->back;
    usize last_layer = rxtree->num_layers - 1;

    for (usize i = 0; i < last_layer; i++) {
        root = root->branches[indices[i]];
        if (root == NULL) return ENOENT;
    }

    void *data = root->leaves[indices[last_layer]];
    if (data == NULL) return ENOENT;

    *out_ptr = data;
    return 0;
}

int radixtree_put(RadixTree *rxtree, u64 index, void *data) {
    spinlock_acq(&rxtree->lock);
    int ret = radixtree_put_nolock(rxtree, index, data);
    spinlock_rel(&rxtree->lock);
    return ret;
}

int radixtree_put_nolock(RadixTree *rxtree, u64 index, void *data) {
    u64 indices[RADIXTREE_MAX_LAYERS] = {0};
    split_index(indices, index, rxtree->num_layers);
    RadixTreeBack *root = rxtree->back;
    usize last_layer = rxtree->num_layers - 1;

    for (usize i = 0; i < last_layer; i++) {
        RadixTreeBack *tmp = root->branches[indices[i]];

        if (tmp == NULL) {
            void *page = (void *)PHYS_TO_HHDM(pmm_allocz());
            tmp = root->branches[indices[i]] = page;
        }

        root = tmp;
    }

    assert(root->leaves[indices[last_layer]] == NULL);
    root->leaves[indices[last_layer]] = data;
    return 0;
}

int radixtree_remove(RadixTree *rxtree, u64 index) {
    spinlock_acq(&rxtree->lock);
    int ret = radixtree_remove_nolock(rxtree, index);
    spinlock_rel(&rxtree->lock);
    return ret;
}

int radixtree_remove_nolock(RadixTree *rxtree, u64 index) {
    u64 indices[RADIXTREE_MAX_LAYERS] = {0};
    split_index(indices, index, rxtree->num_layers);
    RadixTreeBack *root = rxtree->back;
    usize last_layer = rxtree->num_layers - 1;

    for (usize i = 0; i < last_layer; i++) {
        root = root->branches[indices[i]];
        if (root == NULL) return ENOENT;
    }

    assert(root->leaves[indices[last_layer]] != NULL);
    root->leaves[indices[last_layer]] = NULL;
    return 0;
}

void radixtree_init(RadixTree *rxtree, usize layers) {
    assert(layers <= RADIXTREE_MAX_LAYERS);
    rxtree->num_layers = layers;
    rxtree->lock = SPINLOCK_NEW();
    rxtree->back = (void *)PHYS_TO_HHDM(pmm_allocz());
}
