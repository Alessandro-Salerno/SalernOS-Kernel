#pragma once

#include <arch/context.h>
#include <arch/mmu.h>
#include <kernel/com/sync/spinlock.h>
#include <kernel/com/util.h>

#define VMM_FLAGS_NONE      0
#define VMM_FLAGS_ANONYMOUS 1
#define VMM_FLAGS_NOHINT    2
#define VMM_FLAGS_PHYSICAL  4
#define VMM_FLAGS_FILE      8
#define VMM_FLAGS_EXACT     16
#define VMM_FLAGS_SHARED    32
#define VMM_FLAGS_REPLACE   64
#define VMM_FLAGS_ALLOCATE  128
#define VMM_FLAGS_PRIVATE   256

#define VMM_FAULT_ATTR_COW 1 // COW enabled on the address
#define VMM_FAULT_ATTR_MAP 2 // Map on page fault

#define VMM_PREFAULT_SUPPORTED_ATTR (VMM_FAULT_ATTR_MAP)

// Doing this now, but using one big range regardless
typedef enum VMMRangeType {
    VMM_RANGE_TYPE_ANONYMOUS,
    VMM_RANGE_TYPE_SHARED,
    VMM_RANGE_TYPE_FILE
} VMMRangeType;

typedef struct VMMContext {
    MMUPageTable *pagetable;
    usize anon_pages;
    Spinlock lock;
} VMMContext;

void vmm_init(void);
void vmm_init_reaper(void);
VMMContext *vmm_new_context(MMUPageTable *pagetable);
void vmm_destroy_context(VMMContext *context);
VMMContext *vmm_duplicate_context(VMMContext *context);
void *vmm_map(VMMContext *context, void *virt, void *phys, usize len,
              int vmm_flags, MMUFlags mmu_flags);
void *vmm_get_physical(VMMContext *context, void *virt_addr);
void vmm_switch(VMMContext *context);
void vmm_unmap(VMMContext *context, void *virt, usize len);
void vmm_handle_fault(void *fault_virt, void *fault_phys, Context *fault_ctx,
                      MMUFlags mmu_flags_hint, int attr, usize num_pages_hint);
void *vmm_prealloc_range(VMMContext *context, VMMRangeType rangetype,
                         usize len);
