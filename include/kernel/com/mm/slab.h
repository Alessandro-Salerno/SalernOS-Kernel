#pragma once

#include <arch/info.h>
#include <kernel/com/util.h>

// Each slab page starts with a 32-byte header
#define SLAB_MAX_SIZE (PAGE_SIZE - 32)

void *slab_alloc(usize size);
void slab_free(void *ptr, usize size);
#if CONFIG_FAIL_NTH
usize slab_get_num_objects(void);
#endif
