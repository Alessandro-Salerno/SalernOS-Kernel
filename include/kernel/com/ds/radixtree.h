#pragma once

#include <arch/info.h>
#include <kernel/com/sync/spinlock.h>
#include <kernel/com/util.h>
#include <limits.h>

// NOTE: all of this reasonably asumes that RADIXTREE_LAYER_SIZE be even
#define RADIXTREE_LAYER_SIZE (PAGE_SIZE / sizeof(void *))
#define RADIXTREE_INDEX_BITS (__builtin_ffsll(RADIXTREE_LAYER_SIZE) - 1)
#define RADIXTREE_INDEX_MASK (RADIXTREE_LAYER_SIZE - 1)
#define RADIXTREE_MAX_LAYERS                                  \
    ((sizeof(u64) * CHAR_BIT - __builtin_clzll(UINT64_MAX)) / \
     RADIXTREE_INDEX_BITS)

// TODO: implement this
// NOTE: this frees the backing tree, not the whole structure

// Given its definition, sizeof(union RadixTreeBack) == PAGE_SIZE, thus
// this cannot contain radix tree metadata otehrwise it cannot be allocated with
// slab
typedef union RadixTreeBack {
    union RadixTreeBack *branches[RADIXTREE_LAYER_SIZE];
    void *leaves[RADIXTREE_LAYER_SIZE];
} RadixTreeBack;

// This is needed to keep metadata as stated above
// NOTE: This contains a lock, but _nolock operations may be used under other
// locks, the important thing is to be consistent
typedef struct RadixTree {
    usize num_layers;
    RadixTreeBack *back;
    Spinlock lock;
} RadixTree;

RadixTree *radixtree_new(usize num_layers);
int radixtree_free(RadixTree *rxtree);
int radixtree_get(RadixTree *rxtree, u64 index, void **out_ptr);
int radixtree_get_nolock(RadixTree *rxtree, u64 index, void **out_ptr);
int radixtree_put(RadixTree *rxtree, u64 index, void *data);
int radixtree_put_nolock(RadixTree *rxtree, u64 index, void *data);
int radixtree_remove(RadixTree *rxtree, u64 index);
int radixtree_remove_nolock(RadixTree *rxtree, u64 index);

void radixtree_init(RadixTree *rxtree, usize layers);
