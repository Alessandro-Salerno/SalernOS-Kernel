#pragma once

#include <arch/info.h>
#include <kernel/com/util.h>

typedef struct PMMStats {
    usize total;
    usize usable;
    usize reserved;
    usize free;
    usize allocated;
    usize evictable;
    usize to_zero;
    usize to_insert;
} PMMStats;

// z requests zeroed memory; n takes a contiguous page count.
// The _max variants report the actual count through out_alloc_size.
void *pmm_alloc(void);
void *pmm_allocn(usize pages);
void *pmm_allocz(void);
void *pmm_alloczn(usize pages);
void *pmm_allocn_max(usize pages, usize *out_alloc_size);
void *pmm_alloczn_max(usize pages, usize *out_alloc_size);
void pmm_hold(void *page);
bool pmm_is_shared(void *page);
void pmm_free(void *page);
void pmm_freen(void *base, usize pages);
void pmm_unreserven(void *base, usize pages);
void pmm_get_stats(PMMStats *out_pmm_stats);
void pmm_init_threads(void);
void pmm_init(void);
