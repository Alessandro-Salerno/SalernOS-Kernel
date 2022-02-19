#include "User/Input/Keyboard/keyboard.h"
#include "kernelpanic.h"
#include "IO/io.h"


static uint8_t keyboardScancoide;
static uint8_t keyboardASCII;

static bool_t  isShiftPressed;


void kernel_io_keyboard_mods_handle(uint8_t __scancode) {
    switch (keyboardScancoide) {
        case KEY_LSHIFT:
        case KEY_RSHIFT:
            isShiftPressed = TRUE;
            break;
        
        case KEY_LSHIFT + 0x80:
        case KEY_RSHIFT + 0x80:
            isShiftPressed = 0; 
            break;
    }
}

void kernel_io_keyboard_keys_handle() {
    const char _ascii_table[] = {
         0 ,  0 , '1', '2',
        '3', '4', '5', '6',
        '7', '8', '9', '0',
        '-', '=',  0 ,  0 ,
        'q', 'w', 'e', 'r',
        't', 'y', 'u', 'i',
        'o', 'p', '[', ']',
         0 ,  0 , 'a', 's',
        'd', 'f', 'g', 'h',
        'j', 'k', 'l', ';',
        '\'','`',  0 , '\\',
        'z', 'x', 'c', 'v',
        'b', 'n', 'm', ',',
        '.', '/',  0 , '*',
         0 , ' '
    };

    const char _shift_ascii_table[] = {
         0 ,  0 , '!', '@',
        '#', '$', '%', '^',
        '&', '*', '(', ')',
        '_', '+',  0 ,  0 ,
        'Q', 'W', 'E', 'R',
        'T', 'Y', 'U', 'I',
        'O', 'P', '{', '}',
         0 ,  0 , 'A', 'S',
        'D', 'F', 'G', 'H',
        'J', 'K', 'L', ':',
        '\"','~',  0 , '|',
        'Z', 'X', 'C', 'V',
        'B', 'N', 'M', '<',
        '>', '?',  0 , '*',
         0 , ' '
    };
    
    keyboardScancoide = kernel_io_in(0x60);
    keyboardASCII     = 0;
    
    SOFTASSERT(
        keyboardScancoide <= 58         &&
        keyboardScancoide != KEY_LSHIFT &&
        keyboardScancoide != KEY_RSHIFT &&
        keyboardScancoide != KEY_ENTER,

        RETVOID 
    );

    keyboardASCII = (isShiftPressed) ? _shift_ascii_table   [keyboardScancoide] : 
                                       _ascii_table         [keyboardScancoide] ;
}

void kernel_io_keyboard_keys_get(uint8_t* __scancode, uint8_t* __ascii) {
    ARGRET(__scancode, keyboardScancoide);
    ARGRET(__ascii, keyboardASCII);
}
