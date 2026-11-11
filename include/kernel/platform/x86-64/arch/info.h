#pragma once

#include <kernel/com/util.h>
#include <vendor/limine.h>

#define PAGE_SIZE      4096UL
#define NUM_INTERRUPTS 256
#define CACHE_ALIGN    64

#define PHYS_TO_HHDM(addr) ((uptr)0xffff800000000000 + (uptr)(addr))
#define HHDM_TO_PHYS(addr) ((uptr)(addr) - (uptr)0xffff800000000000)

#define PAGE_ROUND(addr) ((uptr)(addr) & ~((uptr)PAGE_SIZE - 1))

#define TIMER_NS (1UL * 1000UL * 1000UL)
#define SCHED_NS (16UL * 1000UL * 1000UL)

// This type must contain the following fields:
//  - u64 (or any integer) type
//  - uptr (or u64) base
//  - u64 (or u64, or usize) length
typedef struct limine_memmap_entry MemMapEntry;

// This type must contain the following fields:
//  - MemMapEntry **entries
//  u64 (or u64, or usize) entry_count
typedef struct limine_memmap_response MemMap;

// This type must contain the following fields:
//  - char *path
//  - char *cmdline
//  - u64 (or u64) size
typedef struct limine_file InfoFile;

// This type must contain an InfoFile pointer named "kernel_file"
typedef struct limine_kernel_file_response KFile;

// This type must contain the following fields:
//  - uptr (or u64) virtual_base
//  - uptr (or u64) physical_base
typedef struct limine_kernel_address_response KAddr;

// This type must contain an integer named "offset"
typedef struct limine_hhdm_response HHDMInfo;

// This type must contain a field named "address" of type void pointer
// This type is never used in src/com, include/kernel/com, or include/lib since
// it is specific to some systems (ACPI)
typedef struct limine_rsdp_response RSDPInfo;

typedef struct limine_framebuffer Framebuffer;

static inline bool memmap_is_usable(const MemMapEntry *entry) {
    return entry->type == LIMINE_MEMMAP_USABLE;
}

static inline bool memmap_is_reclaimable(const MemMapEntry *entry) {
    return entry->type == LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE ||
        entry->type == LIMINE_MEMMAP_KERNEL_AND_MODULES;
}

static inline bool memmap_is_mappable(const MemMapEntry *entry) {
    return entry->type == LIMINE_MEMMAP_USABLE ||
        entry->type == LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE ||
        entry->type == LIMINE_MEMMAP_KERNEL_AND_MODULES ||
        entry->type == LIMINE_MEMMAP_FRAMEBUFFER;
}

static inline bool memmap_is_framebuffer(const MemMapEntry *entry) {
    return entry->type == LIMINE_MEMMAP_FRAMEBUFFER;
}
