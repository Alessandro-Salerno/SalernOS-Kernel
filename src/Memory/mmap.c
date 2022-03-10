#include "Memory/mmap.h"
#include "kernelpanic.h"

#define MMAP_DINIT   "EFI Memory Map Fault:\nKernel tried to reinitialize Memory Map."
#define MMAP_NINIT   "EFI Memory Map Fault:\nKernel tried to fetch information before initializing."


static meminfo_t mInfo;

static void*    mMap;
static uint64_t mMapSize;
static uint64_t mMapEntries;
static uint64_t mMapDescSize;

static uint64_t mSize;
static uint64_t mUsableSize;

static memseg_t mLargestSegment;

static bool_t mMapInitialized;


void kernel_mmap_initialize(meminfo_t __meminfo) {
    kernel_panic_assert(!mMapInitialized, MMAP_DINIT);

    mInfo        = __meminfo;
    mMap         = __meminfo._MemoryMap;
    mMapSize     = __meminfo._MemoryMapSize;
    mMapDescSize = __meminfo._DescriptorSize;

    mMapEntries  = mMapSize / mMapDescSize;

    for (uint64_t _idx = 0; _idx < mMapEntries; _idx++) {
        efimemdesc_t* _entry       = kernel_mmap_entry_get(_idx);
        uint64_t      _seg_sz      = _entry->_Pages * 4096;
                      mSize       += _seg_sz;
                      mUsableSize += (_entry->_Type == 7) ? _seg_sz : 0;

        if (_entry->_Type == 7 && _seg_sz > mLargestSegment._Size) {
            mLargestSegment = (memseg_t) {
                ._Segment = _entry->_PhysicalAddress,
                ._Size    = _seg_sz
            };
        }
    }

    mMapInitialized = TRUE;
}

void kernel_mmap_info_get(uint64_t* __memsz, uint64_t* __usablemem, memseg_t* __lseg, meminfo_t* __meminfo) {
    kernel_panic_assert(mMapInitialized, MMAP_NINIT);

    ARGRET(__memsz, mSize);
    ARGRET(__usablemem, mUsableSize);
    ARGRET(__lseg, mLargestSegment);
    ARGRET(__meminfo, mInfo);
}

efimemdesc_t* kernel_mmap_entry_get(uint64_t __idx) {
    kernel_panic_assert(mMap != NULL, MMAP_NINIT);
    return (efimemdesc_t*)((uint64_t)(mMap) + (__idx * mMapDescSize));
}
