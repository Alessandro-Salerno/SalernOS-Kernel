#include <arch/info.h>
#include <kernel/com/crt/string.h>
#include <kernel/com/fs/pagecache.h>
#include <kernel/com/mm/pmm.h>
#include <kernel/com/util.h>
#include <strings.h>

// This is all temporary while I rewrite Vfs

bool pagecache_get(PageCache *root, u64 index, uptr *out_addr) {
    void *out_void;
    if (radixtree_get_nolock(root, index, &out_void) != 0) return false;
    *out_addr = (uptr)out_void;
    return true;
}

void pagecache_default(PageCache *root, u64 index, uptr *out_addr) {
    if (pagecache_get(root, index, out_addr)) return;

    void *data = (void *)PHYS_TO_HHDM(pmm_allocz());
    radixtree_put_nolock(root, index, data);
    *out_addr = (uptr)data;
}

PageCache *pagecache_new(void) { return radixtree_new(PAGECACHE_LAYERS); }

static void free_layer(RadixTreeBack *node, usize layers_left) {
    for (usize i = 0; i < RADIXTREE_LAYER_SIZE; i++) {
        if (layers_left > 1 && node->branches[i] != NULL) {
            free_layer(node->branches[i], layers_left - 1);
        } else if (layers_left == 1 && node->leaves[i] != NULL) {
            pmm_free((void *)HHDM_TO_PHYS(node->leaves[i]));
        }
    }

    pmm_free((void *)HHDM_TO_PHYS(node));
}

void pagecache_free(PageCache *root) {
    free_layer(root->back, root->num_layers);
    radixtree_free(root);
}
