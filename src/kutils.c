#include <kerninc.h>


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

    kernel_shell_printf("DEBUG: Initializing GDT...\n");
    kernel_gdt_load(&_gdt_desc);
}

void kernel_kutils_mem_setup(boot_t __bootinfo) {
    uint64_t _mem_size;
    uint64_t _fbbase = (uint64_t)(__bootinfo._Framebuffer->_BaseAddress);
    size_t   _fbsize = __bootinfo._Framebuffer->_BufferSize + 4096;
     
    kernel_shell_printf("DEBUG: Initializing EFI Memory Map...\n");
    kernel_memory_init(__bootinfo._Memory);
    kernel_memory_get_sizes(&_mem_size, NULL, NULL);

    kernel_shell_printf("DEBUG: Initializing Kernel Page Frame Allocator...\n");
    kernel_allocator_initialize();

    kernel_shell_printf("DEBUG: Locking Kernel Pages...\n");
    uint64_t _kernel_size  = (uint64_t)(&_KERNEL_END) - (uint64_t)(&_KERNEL_START);
    uint64_t _kernel_pages = (uint64_t)(_kernel_size) / 4096 + 1;
    kernel_allocator_lock_pages(&_KERNEL_START, _kernel_pages);

    kernel_shell_printf("DEBUG: Initializing Page Table Manager...\n");
    pgtable_t* _lvl4 = (pgtable_t*)(kernel_allocator_allocate_page());
    kernel_memory_set((void*)(_lvl4), 4096, 0);

    pgtm_t _page_table_manager = (pgtm_t) { 
        ._PML4Address = _lvl4
    };

    for (uint64_t t = 0; t < _mem_size; t += 4096)
        kernel_paging_map_address(&_page_table_manager, (void*)(t), (void*)(t));

    for (uint64_t t = _fbbase; t < _fbbase + _fbsize; t += 4096)
        kernel_paging_map_address(&_page_table_manager, (void*)(t), (void*)(t));

    asm ("mov %0, %%cr3" : : "r" (_lvl4)); 
    
    kernel_shell_printf("DEBUG: Locking Font and Framebuffer Pages...\n");
    kernel_allocator_lock_pages((void*)(_fbbase), _fbsize / 4096 + 1);
    kernel_allocator_lock_pages((void*)(__bootinfo._Font->_Buffer), (__bootinfo._Font->_Header->_CharSize * 256) / 4096);
}

void kernel_kutils_int_setup() {
    kernel_shell_printf("DEBUG: Initializing Interrupts...\n");
    kernel_idt_initialize();
    kernel_interrupts_pic_remap();
}