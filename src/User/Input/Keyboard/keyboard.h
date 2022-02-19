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
    void            kernel_io_keyboard_mods_handle      (uint8_t __scancode);
    void            kernel_io_keyboard_keys_handle      ();
    void            kernel_io_keyboard_keys_get         (uint8_t* __scancode, uint8_t* __ascii);

#endif
