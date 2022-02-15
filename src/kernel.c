#include <kerninc.h>


void kernel_main(boot_t __bootinfo) {
    framebuffer_t* _buff = __bootinfo._Framebuffer;
    bmpfont_t*     _font = __bootinfo._Font;

    uint8_t _seb_minver_day   = __bootinfo._SEBMinorVersion ^ ((__bootinfo._SEBMinorVersion >> 8) << 8);
    uint8_t _seb_minver_month = (__bootinfo._SEBMinorVersion ^ _seb_minver_day) >> 8;

    uint64_t _mem_size,
             _usable_mem,
             _free_mem,
             _used_mem,
             _unusable_mem;

    kernel_kutils_kdd_setup(__bootinfo);
    kernel_kutils_gdt_setup();
    kernel_kutils_mem_setup(__bootinfo);
    kernel_kutils_int_setup();
    kernel_memory_get_sizes(&_mem_size, &_usable_mem, NULL);

    kernel_shell_printf("DEBUG: Testing...\n");
    void*      _test_page    = kernel_allocator_allocate_page();
    pgmapidx_t _indexer_test = kernel_paging_index(0x1000 * 52 + 0x50000 * 7);

    kernel_text_reinitialize(
        FGCOLOR, BGCOLOR,
        0, 0
    );

    kernel_shell_printf("Copyright 2021 - 2022 Alessandro Salerno. All rights reserved.\n");
    kernel_shell_printf("SalernOS EFI Bootloader %u-%c%u\n", __bootinfo._SEBMajorVersion, _seb_minver_month, _seb_minver_day);
    kernel_shell_printf("SalernOS Kernel 0.0.5 (Florence)\n\n");

    kernel_shell_printf("Framebuffer Resolution: %u x %u\n", _buff->_PixelsPerScanLine, _buff->_Height);
    kernel_shell_printf("Framebuffer Base Address: %u\n", _buff->_BaseAddress);
    kernel_shell_printf("Framebuffer Size: %u bytes\n", _buff->_BufferSize);
    kernel_shell_printf("Framebuffer BPP: %u bytes\n\n", _buff->_BytesPerPixel);

    kernel_shell_printf("Memory Map Size: %u bytes\n", __bootinfo._Memory._MemoryMapSize);
    kernel_shell_printf("Memory Map Descriptor Size: %u bytes\n\n", __bootinfo._Memory._DescriptorSize);

    kernel_allocator_get_infO(&_free_mem, &_used_mem, &_unusable_mem);
    kernel_shell_printf("System Memory: %u bytes\n", _mem_size);
    kernel_shell_printf("Usable Memory: %u bytes\n", _usable_mem);
    kernel_shell_printf("Free Memory: %u bytes\n", _free_mem);
    kernel_shell_printf("Used Memory: %u bytes\n", _used_mem);
    kernel_shell_printf("Reserved Memory: %u bytes\n", _unusable_mem);
    kernel_shell_printf("Test Page Address: %u\n", (uint64_t)(_test_page));
    kernel_shell_printf("Page Indexer Test: %d-%d-%d-%d\n\n\n", _indexer_test._PageIndex,
                                                                _indexer_test._PageTableIndex,
                                                                _indexer_test._PageDirectoryIndex,
                                                                _indexer_test._PageDirectoryPointerIndex);

    kernel_shell_printf("Kernel Ready!\n\n!");
    
    // What follows is just debug code
    // This will be removed in a later version
    uint8_t _kcode, _nkcode, _ascii;
    while (1) {
        kernel_io_keyboard_read_keys();
        kernel_io_keyboard_get_info(&_nkcode, &_ascii);
        
        if (_nkcode == _kcode)
            continue;
        
        _kcode = _nkcode;

        switch (_kcode) {
            case KEY_ENTER:
                kernel_shell_printf("\n");
                break;

            case KEY_SPACE:
                kernel_shell_printf(" ");
                break;

            case KEY_BACKSPACE: {
                kernel_syscall_dispatch("\n\nHello System call!\0", SYSCALL_PRINT_STR);
            } break;

            default:
                kernel_shell_printf("%c", _ascii);
                break;
        }
    }
}
