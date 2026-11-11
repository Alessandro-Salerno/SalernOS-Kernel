#pragma once

#include <arch/mmu.h>
#include <kernel/com/util.h>

void mmu_init(void);
MMUPageTable *mmu_new_table(void);
void mmu_destroy_table(MMUPageTable *pt);
MMUPageTable *mmu_duplicate_table(MMUPageTable *pt);
bool mmu_map(MMUPageTable *pt, void *virt, void *phys, MMUFlags flags);
bool mmu_chflags(MMUPageTable *pt, void *virt, MMUFlags new_flags);
bool mmu_unmap(MMUPageTable *pt, void *virt, void **out_old_phys);
void mmu_invalidate(MMUPageTable *pt, void *virt, usize pages);
void mmu_switch(MMUPageTable *pt);
void mmu_switch_default(void);
void *mmu_get_physical(MMUPageTable *pagetable, void *virt_addr,
                       MMUFlags *out_flags, int *out_attr);
bool mmu_is_cow(MMUPageTable *pagetable, void *virt_addr);
bool mmu_is_executable(MMUPageTable *pagetable, void *virt_addr);
MMUPageTable *mmu_get_table(void);
