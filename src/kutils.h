#ifndef SALERNOS_CORE_KERNEL_UTILITIES
#define SALERNOS_CORE_KERNEL_UTILITIES

    #include <kerntypes.h>
    #include "user/Output/Display/kdd.h"
    #include "Memory/efimem.h"

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

    /*****************************************************************
    RET TYPE        FUNCTION NAME                 FUNCTION ARGUMENTS
    *****************************************************************/
    void            kernel_kutils_kdd_setup       (boot_t __bootinfo);
    void            kernel_kutils_gdt_setup       ();
    void            kernel_kutils_mem_setup       (boot_t __bootinfo);
    void            kernel_kutils_int_setup       ();

#endif
