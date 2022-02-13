#ifndef SALERNOS_CORE_KERNEL_TEXT
#define SALERNOS_CORE_KERNEL_TEXT

    #include "User/Output/Display/kdd.h"
    #include "kerntypes.h"


    void kernel_text_print_char    (char __char);
    void kernel_text_print_str     (char* __str);

    void kernel_text_move_caret    (int64_t __xoff, int64_t __yoff);
    void kernel_text_get_handles   (uint32_t* __color, uint32_t* __backcolor, uint32_t* __xoff, uint32_t* __yoff, bmpfont_t** __font);
    void kernel_text_initialize    (uint32_t __color, uint32_t __backcolor, uint32_t __xoff, uint32_t __yoff, bmpfont_t* __font); 
    void kernel_text_reinitialize  (uint32_t __color, uint32_t __backcolor, uint32_t __xoff, uint32_t __yoff);

    void kernel_text_scroll_up     (uint32_t __lines);
    void kernel_text_scroll_down   (uint32_t __lines);

    void kernel_text_clear_line    (uint32_t __line);
    void kernel_text_set_color     (uint32_t __color, uint32_t __backcolor);

#endif
