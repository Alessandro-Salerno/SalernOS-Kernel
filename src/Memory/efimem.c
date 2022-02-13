#include "Memory/efimem.h"
#include "kernelpanic.h"

#define MEM_DINIT   "EFI Memory Map Fault:\nKernel tried to reinitialize Memory Map."
#define MEM_NINIT   "EFI Memory Map Fault:\nKernel tried to fetch information before initializing."
#define MEM_OPNINIT "EFI Memory Map Fault:\nKernel tried to perform a memory operation before initializing."


static void*    memoryMap;
static uint64_t MEMORY_MAP_SIZE;
static uint64_t MEMORY_DESCRIPTOR_SIZE;
static uint64_t MEMORY_MAP_ENTRIES;

static uint64_t  MEMORY_SIZE;
static uint64_t  USABLE_MEMORY;
static uint64_t  BOOTLOADER_MEMORY;
static uint64_t  UNUSABLE_MEMORY;

static uint64_t LARGEST_MEM_SEG_SIZE;
static void*    LARGEST_MEM_SEG;

static bool_t memoryInitialized = 0;


void kernel_memory_init(meminfo_t __meminfo) {
    kernel_panic_assert(!memoryInitialized, MEM_DINIT);

    memoryMap              = __meminfo._MemoryMap;
    MEMORY_MAP_SIZE        = __meminfo._MemoryMapSize;
    MEMORY_DESCRIPTOR_SIZE = __meminfo._DescriptorSize;

    MEMORY_MAP_ENTRIES     = MEMORY_MAP_SIZE / MEMORY_DESCRIPTOR_SIZE;

    for (uint64_t _i = 0; _i < MEMORY_MAP_ENTRIES; _i++) {
        efimemdesc_t*  _mem_desc = (efimemdesc_t*)((uint64_t)(memoryMap) + (_i * MEMORY_DESCRIPTOR_SIZE));
        MEMORY_SIZE             += _mem_desc->_Pages * 4096;

        switch (_mem_desc->_Type) {
            case 0:
            case 8: { UNUSABLE_MEMORY += _mem_desc->_Pages * 4096; } break;

            case 1:
            case 2:
            case 3:
            case 4: { BOOTLOADER_MEMORY += _mem_desc->_Pages * 4096; } break;

            case 7: {
                uint64_t _seg_size = _mem_desc->_Pages * 4096;
                USABLE_MEMORY     += _seg_size;

                if (_seg_size > LARGEST_MEM_SEG_SIZE) {
                    LARGEST_MEM_SEG_SIZE = _seg_size;
                    LARGEST_MEM_SEG      = _mem_desc->_PhysicalAddress;
                }
            } break;
        }
    }

    memoryInitialized = 1;
}

void kernel_memory_get_sizes(uint64_t* __memsize, uint64_t* __usablemem, uint64_t* __bootmem) {
    kernel_panic_assert(memoryInitialized, MEM_NINIT);

    ARGRET(__memsize, MEMORY_SIZE);
    ARGRET(__usablemem, USABLE_MEMORY);
    ARGRET(__bootmem, BOOTLOADER_MEMORY);
}

void kernel_memory_get_largest_segment(uint64_t* __sz, void** __seg) {
    kernel_panic_assert(memoryInitialized, MEM_NINIT);

    ARGRET(__sz, LARGEST_MEM_SEG_SIZE);
    ARGRET(__seg, LARGEST_MEM_SEG);
}

void kernel_memory_set(void* __buff, const uint64_t __buffsize, const uint8_t __val) {
    kernel_panic_assert(memoryInitialized, MEM_OPNINIT);
    __uint128_t _128bit_val = __val;

    if (__buffsize < 16) {
        for (uint8_t _i = 0; _i < __buffsize; _i++)
            *(uint8_t*)((uint64_t)(__buff) + _i) = __val;
        
        return;
    }

    if (__val != 0) {
        for (uint8_t _i = 8; _i < 128; _i += 8)
            _128bit_val |= __val << _i;
    }

    for (uint64_t _i = 0; _i < __buffsize; _i += 16)
        *(__uint128_t*)((uint64_t)(__buff) + _i) = _128bit_val;

    for (uint64_t _i = __buffsize - (__buffsize % 16); _i < __buffsize; _i++)
        *(uint8_t*)((uint64_t)(__buff) + _i) = _128bit_val;
}

const int kernel_memory_get_map_entries() {
    kernel_panic_assert(memoryInitialized, MEM_OPNINIT);
    return MEMORY_MAP_ENTRIES;
}

efimemdesc_t* kernel_memory_get_map_entry(const uint64_t __idx) {
    kernel_panic_assert(memoryInitialized, MEM_NINIT);
    return (efimemdesc_t*)((uint64_t)(memoryMap) + (__idx * MEMORY_DESCRIPTOR_SIZE));
}
