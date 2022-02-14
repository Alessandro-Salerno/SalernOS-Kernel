// User Input includes
#include "User/Input/Keyboard/keyboard.h"       /*   Includes the basic Kernel Keyboard Driver          */

// User Output includes
#include "User/Output/Text/textrenderer.h"      /*   Includes the basic Kernel Text Renderer            */
#include "User/Output/Text/kernelshell.h"       /*   Includes the basic Kernel Text Formatter           */
#include "User/Output/Display/kdd.h"            /*   Includes the basic Kernel Display Driver           */
#include "User/Output/Text/cstr.h"              /*   Includes the basic Kernel String Utilities         */

// Memory includes
#include "Memory/efimem.h"                      /*   Includes the Kernel EFI Memory Map Interface       */
#include "Memory/palloc.h"                      /*   Includes the Kernel Page Frame Allocator           */
#include "Memory/paging.h"                      /*   Includes the Kernel Page Table Manager             */
#include "Memory/bmp.h"                         /*   Includes the Kernel's definition of a Bitmap       */

// System includes
#include "Interrupts/handlers.h"                /*   Includes the Kernel's Interrupt Service Routines   */
#include "Interrupts/idt.h"                     /*   Includes the Kernel's definition of the IDT        */
#include "Interrupts/pic.h"                     /*   Includes the basic Kernel PIC Driver               */
#include "GDT/gdt.h"                            /*   Includes the Kernel's definition of the GTD        */
#include "IO/io.h"                              /*   Includes the basic Kernel I/O functions            */

// Syscall includes
#include "Syscall/dispatcher.h"                 /*   Includes the Kernel Syscall Dispatcher functions   */
#include "Syscall/syscalls.h"                   /*   Includes all kernel Syscall declarations           */

// Macros and constants
#define FGCOLOR kernel_kdd_pxcolor_translate(255, 255, 255, 255)
#define BGCOLOR kernel_kdd_pxcolor_translate(0, 0, 50, 255)


typedef struct BootInfo {
    framebuffer_t* _Framebuffer;
    bmpfont_t*     _Font;
    meminfo_t      _Memory;

    uint8_t        _SEBMajorVersion;
    uint16_t       _SEBMinorVersion;
} boot_t;

extern uint64_t _KERNEL_START,
                _KERNEL_END;

void kernel_main(boot_t __bootinfo) {
    gdtdesc_t _gdt_desc = (gdtdesc_t) {
        ._Size   = sizeof(gdt_t) - 1,
        ._Offset = (uint64_t)(&gdt)
    };

    framebuffer_t* _buff = __bootinfo._Framebuffer;
    bmpfont_t*     _font = __bootinfo._Font;

    uint8_t _seb_minver_day   = __bootinfo._SEBMinorVersion ^ ((__bootinfo._SEBMinorVersion >> 8) << 8);
    uint8_t _seb_minver_month = (__bootinfo._SEBMinorVersion ^ _seb_minver_day) >> 8;

    uint64_t _mem_size,
             _usable_mem,
             _free_mem,
             _used_mem,
             _unusable_mem;

    kernel_kdd_fbo_bind(_buff);

    kernel_text_initialize(
        FGCOLOR, BGCOLOR,
        0, 0, _font
    );

    kernel_shell_printf("DEBUG: Initializing GDT...\n");
    kernel_gdt_load(&_gdt_desc);

    kernel_shell_printf("DEBUG: Initializing EFI Memory Map...\n");
    kernel_memory_init(__bootinfo._Memory);
    kernel_memory_get_sizes(&_mem_size, &_usable_mem, NULL);

    kernel_shell_printf("DEBUG: Initializing Kernel Page Frame Allocator...\n");
    kernel_allocator_initialize();

    kernel_shell_printf("DEBUG: Locking Kernel Pages...\n");
    uint64_t _kernel_size  = (uint64_t)(&_KERNEL_END) - (uint64_t)(&_KERNEL_START);
    uint64_t _kernel_pages = (uint64_t)(_kernel_size) / 4096 + 1;
    kernel_allocator_lock_pages(&_KERNEL_START, _kernel_pages);

    kernel_shell_printf("DEBUG: Initializing Page Table Manager...\n");
    pgtable_t* _lvl4 = (pgtable_t*)(kernel_allocator_allocate_page());
    kernel_memory_set((void*)(_lvl4), 4096, 0);

    pgtm_t _page_table_manager = (pgtm_t) { ._PML4Address = _lvl4  };

    for (uint64_t t = 0; t < _mem_size; t += 4096)
        kernel_paging_map_address(&_page_table_manager, (void*)(t), (void*)(t));

    uint64_t _fbbase = (uint64_t)(_buff->_BaseAddress);
    size_t   _fbsize = _buff->_BufferSize + 4096;

    for (uint64_t t = _fbbase; t < _fbbase + _fbsize; t += 4096)
        kernel_paging_map_address(&_page_table_manager, (void*)(t), (void*)(t));

    asm ("mov %0, %%cr3" : : "r" (_lvl4)); 
    
    kernel_shell_printf("DEBUG: Locking Font and Framebuffer Pages...\n");
    kernel_allocator_lock_pages((void*)(_fbbase), _fbsize / 4096 + 1);
    kernel_allocator_lock_pages((void*)(_font->_Buffer), (_font->_Header->_CharSize * 256) / 4096);

    kernel_shell_printf("DEBUG: Initializing Interrupts...\n");
    kernel_idt_initialize();
    kernel_interrupts_pic_remap();

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
