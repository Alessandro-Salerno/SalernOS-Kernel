#ifndef SALERNOS_CORE_KERNEL_TEXT
#define SALERNOS_CORE_KERNEL_TEXT

    #include "User/Output/Display/kdd.h"
    #include <kerntypes.h>

    #define CURSOR_CHARACTER '_'


    /************************************************************************************************************************************************
    RET TYPE        FUNCTION NAME                 FUNCTION ARGUMENTS
    ************************************************************************************************************************************************/
    void            kernel_text_blitch            (char __char);
    void            kernel_text_putch             (char __char);
    void            kernel_text_print             (char* __str);

    void            kernel_text_initialize        (uint32_t __color, uint32_t __backcolor, uint32_t __xoff, uint32_t __yoff, bmpfont_t* __font); 
    void            kernel_text_reinitialize      (uint32_t __color, uint32_t __backcolor, uint32_t __xoff, uint32_t __yoff);

    void            kernel_text_scroll            (uint32_t __lines);
    void            kernel_text_line_clear        (uint32_t __line);
    
    void            kernel_text_info_set          (uint32_t __color, uint32_t __backcolor, uint32_t __xoff, uint32_t __yoff);
    void            kernel_text_info_get          (uint32_t* __color, uint32_t* __backcolor, uint32_t* __xoff, uint32_t* __yoff, bmpfont_t** __font);

#endif
