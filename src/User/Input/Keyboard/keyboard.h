#ifndef SALERNOS_CORE_KERNEL_USER_INPUT_KEYBOARD
#define SALERNOS_CORE_KERNEL_USER_INPUT_KEYBOARD

    #include "kerntypes.h"

    #define KEY_LSHIFT    0x2A
    #define KEY_RSHIFT    0x36
    #define KEY_ENTER     0x1C
    #define KEY_BACKSPACE 0x0E
    #define KEY_SPACE     0x39


    /*******************************************************************************************
    RET TYPE        FUNCTION NAME                       FUNCTION ARGUMENTS
    *******************************************************************************************/
    void            kernel_io_keyboard_handle_mods      (uint8_t __scancode);
    void            kernel_io_keyboard_read_keys        ();
    void            kernel_io_keyboard_get_info         (uint8_t* __scancode, uint8_t* __ascii);

#endif
