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
    kernel_mmap_info_get(&_mem_size, &_usable_mem, NULL, NULL);

    kprintf("DEBUG: Testing...\n");
    void* _test_page = kernel_allocator_allocate_page();

    kernel_text_reinitialize(
        FGCOLOR, BGCOLOR,
        0, 0
    );

    kprintf("Copyright 2021 - 2022 Alessandro Salerno. All rights reserved.\n");
    kprintf("SalernOS EFI Bootloader %u-%c%u\n", __bootinfo._SEBMajorVersion, _seb_minver_month, _seb_minver_day);
    kprintf("SalernOS Kernel 0.0.5 (Florence)\n\n");

    kprintf("Framebuffer Resolution: %u x %u\n", _buff->_PixelsPerScanLine, _buff->_Height);
    kprintf("Framebuffer Base Address: %u\n", _buff->_BaseAddress);
    kprintf("Framebuffer Size: %u bytes\n", _buff->_BufferSize);
    kprintf("Framebuffer BPP: %u bytes\n\n", _buff->_BytesPerPixel);

    kprintf("Memory Map Size: %u bytes\n", __bootinfo._Memory._MemoryMapSize);
    kprintf("Memory Map Descriptor Size: %u bytes\n\n", __bootinfo._Memory._DescriptorSize);

    kernel_allocator_get_infO(&_free_mem, &_used_mem, &_unusable_mem);
    kprintf("System Memory: %u bytes\n", _mem_size);
    kprintf("Usable Memory: %u bytes\n", _usable_mem);
    kprintf("Free Memory: %u bytes\n", _free_mem);
    kprintf("Used Memory: %u bytes\n", _used_mem);
    kprintf("Reserved Memory: %u bytes\n", _unusable_mem);
    kprintf("Test Page Address: %u\n\n\n", (uint64_t)(_test_page));

    kprintf("Kernel Ready!\n\n");
    
    // What follows is just debug code
    // This will be removed in a later version
    uint8_t _kcode, _nkcode, _ascii;
    while (1) {
        kernel_io_keyboard_keys_handle();
        kernel_io_keyboard_keys_get(&_nkcode, &_ascii);
        
        if (_nkcode == _kcode)
            continue;
        
        _kcode = _nkcode;

        switch (_kcode) {
            case KEY_ENTER:
                kprintf("\n");
                break;

            case KEY_SPACE:
                kprintf(" ");
                break;

            case KEY_BACKSPACE: {
                kernel_syscall_dispatch("\n\nHello System call!\0", SYSCALL_PRINT_STR);
            } break;

            default:
                kprintf("%c", _ascii);
                break;
        }
    }
}
