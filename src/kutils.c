#include <kerninc.h>
#include <kmem.h>
#include <kdebug.h>


void kernel_kutils_kdd_setup(boot_t __bootinfo) {
    kernel_kdd_fbo_bind(__bootinfo._Framebuffer);

    kernel_text_initialize(
        FGCOLOR, BGCOLOR,
        0, 0, __bootinfo._Font
    );
}

void kernel_kutils_gdt_setup() {
    gdtdesc_t _gdt_desc = (gdtdesc_t) {
        ._Size   = sizeof(gdt_t) - 1,
        ._Offset = (uint64_t)(&gdt)
    };

    klogdebug("Initializing GDT...\n");
    kernel_gdt_load(&_gdt_desc);
}

void kernel_kutils_mem_setup(boot_t __bootinfo) {
    uint64_t _mem_size;
    void*    _fbbase = __bootinfo._Framebuffer->_BaseAddress;
    size_t   _fbsize = __bootinfo._Framebuffer->_BufferSize + 4096;
    
    klogdebug("Initializing EFI Memory Map...\n");
    kernel_mmap_initialize(__bootinfo._Memory);
    kernel_mmap_info_get(&_mem_size, NULL, NULL, NULL);

    klogdebug("Initializing Kernel Page Frame Allocator...\n");
    kernel_allocator_initialize();

    klogdebug("Locking Kernel Pages...\n");
    uint64_t _kernel_size  = (uint64_t)(&_KERNEL_END) - (uint64_t)(&_KERNEL_START);
    uint64_t _kernel_pages = (uint64_t)(_kernel_size) / 4096 + 1;
    kernel_allocator_lock(&_KERNEL_START, _kernel_pages);

    klogdebug("Initializing Page Table Manager...\n");
    pgtable_t* _lvl4 = (pgtable_t*)(kernel_allocator_page_new());
    memset(_lvl4, 4096, 0);

    pgtm_t _page_table_manager = (pgtm_t) { 
        ._PML4Address = _lvl4
    };

    kernel_paging_address_mapn(&_page_table_manager, (void*)(0), _mem_size);
    kernel_paging_address_mapn(&_page_table_manager, (void*)(_fbbase), _fbsize);

    asm ("mov %0, %%cr3" : : "r" (_lvl4)); 
    
    klogdebug("Locking Font and Framebuffer Pages...\n");
    kernel_allocator_reserve(_fbbase, _fbsize / 4096 + 1);
    kernel_allocator_reserve(__bootinfo._Font->_Buffer, (__bootinfo._Font->_Header->_CharSize * 256) / 4096);
}

void kernel_kutils_int_setup() {
    klogdebug("Initializing Interrupts...\n");
    kernel_idt_initialize();
    kernel_interrupts_pic_remap();
}
