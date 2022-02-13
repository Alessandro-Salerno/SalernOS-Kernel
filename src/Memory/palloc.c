#include "Memory/palloc.h"
#include "kernelpanic.h"

#define PALLOC_FAULT "Page Allocator Fault:\nAllocator Function called before initialization."
#define PALLOC_DBMP  "Page Allocator Fault:\nAllocator tried to reinitialize page bitmap."


static uint64_t freeMemory      = 0,
                reservedMemory  = 0,
                usedMemory      = 0,
                pgBmpIndex      = 0;

static bmp_t    bitmap;
static uint64_t BMPSEGSZ;
static void*    BMPSEG;

static uint8_t pallocInitialized;


static void __reserve_page__(void* __address) {
    kernel_panic_assert(pallocInitialized >= 2, PALLOC_FAULT);
    
    uint64_t _idx = (uint64_t)(__address) / 4096;

    SOFTASSERT(!(kernel_bmp_get(&bitmap, _idx)), RETVOID)
    
    kernel_bmp_set(&bitmap, _idx, 1);
    freeMemory     -= 4096;
    reservedMemory += 4096;
}

static void __reserve_pages__(void* __address, uint64_t __pagecount) {
    for (uint64_t _i = 0; _i < __pagecount; _i++)
        __reserve_page__((void*)((uint64_t)(__address) + (_i * 4096))); 
}

static void __unreserve_page__(void* __address) {
    kernel_panic_assert(pallocInitialized >= 2, PALLOC_FAULT);
    
    uint64_t _idx = (uint64_t)(__address) / 4096;

    SOFTASSERT(kernel_bmp_get(&bitmap, _idx), RETVOID)
    
    kernel_bmp_set(&bitmap, _idx, 0);
    freeMemory     += 4096;
    reservedMemory -= 4096;

    if (pgBmpIndex > _idx)
        pgBmpIndex = _idx;
}

static void __unreserve_pages__(void* __address, uint64_t __pagecount) {
    for (uint64_t _i = 0; _i < __pagecount; _i++)
        __unreserve_page__((void*)((uint64_t)(__address) + (_i * 4096))); 
}

static void __init__bitmap__() {
    kernel_panic_assert(pallocInitialized == 1, PALLOC_DBMP);
    
    kernel_memory_set(BMPSEG, BMPSEGSZ, 0);

    bitmap = (bmp_t) {
        ._Size = freeMemory / 4096 / 8 + 1,
        ._Buffer = (uint8_t*)(BMPSEG)
    };

    pallocInitialized = 2;
    kernel_allocator_lock_pages(bitmap._Buffer, bitmap._Size / 4096 + 1);
}

void kernel_allocator_initialize() {
    SOFTASSERT(pallocInitialized == 0, RETVOID);
    
    kernel_memory_get_sizes(&freeMemory, NULL, NULL);
    kernel_memory_get_largest_segment(&BMPSEGSZ, &BMPSEG);

    pallocInitialized = 1;
    __init__bitmap__();
    
    for (uint64_t _i = 0; _i < kernel_memory_get_map_entries(); _i++) {
        efimemdesc_t* _mem_desc = kernel_memory_get_map_entry(_i);

        if (_mem_desc->_Type != 7)
            __reserve_pages__(_mem_desc->_PhysicalAddress, _mem_desc->_Pages); 
    }

    pallocInitialized = 3;
}

void kernel_allocator_free_page(void* __address) {
    kernel_panic_assert(pallocInitialized >= 2, PALLOC_FAULT);

    uint64_t _idx = (uint64_t)(__address) / 4096;

    SOFTASSERT(kernel_bmp_get(&bitmap, _idx), RETVOID)
    
    kernel_bmp_set(&bitmap, _idx, 0);
    freeMemory += 4096;
    usedMemory -= 4096;

    if (pgBmpIndex > _idx)
        pgBmpIndex = _idx;
}

void kernel_allocator_lock_page(void* __address) {
    kernel_panic_assert(pallocInitialized >= 2, PALLOC_FAULT);

    uint64_t _idx = (uint64_t)(__address) / 4096;

    SOFTASSERT(!(kernel_bmp_get(&bitmap, _idx)), RETVOID)
    
    kernel_bmp_set(&bitmap, _idx, 1);
    freeMemory -= 4096;
    usedMemory += 4096;
}

void kernel_allocator_free_pages(void* __address, uint64_t __pagecount) {
    for (uint64_t _i = 0; _i < __pagecount; _i++)
        kernel_allocator_free_page((void*)((uint64_t)(__address) + (_i * 4096))); 
}

void kernel_allocator_lock_pages(void* __address, uint64_t __pagecount) {
    for (uint64_t _i = 0; _i < __pagecount; _i++)
        kernel_allocator_lock_page((void*)((uint64_t)(__address) + (_i * 4096))); 
}

void kernel_allocator_get_infO(uint64_t* __freemem, uint64_t* __usedmem, uint64_t* __reservedmem) { 
    ARGRET(__freemem, freeMemory);
    ARGRET(__usedmem, usedMemory);
    ARGRET(__reservedmem, reservedMemory);
}

void* kernel_allocator_allocate_page() {
    kernel_panic_assert(pallocInitialized >= 2, PALLOC_FAULT);

    for (; pgBmpIndex < bitmap._Size * 8; pgBmpIndex++) {
        if (kernel_bmp_get(&bitmap, pgBmpIndex) == 0) {
            void* _page = (void*)(pgBmpIndex * 4096);
            kernel_allocator_lock_page(_page);
            return _page;
        }
    }

    return NULL; // Swap file not implemented yet
}
