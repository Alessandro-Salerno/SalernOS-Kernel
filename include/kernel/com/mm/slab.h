#pragma once

#include <kernel/com/util.h>

void *slab_alloc(usize size);
void slab_free(void *ptr, usize size);
