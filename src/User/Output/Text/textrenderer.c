#include "User/Output/Text/textrenderer.h"
#include "kernelpanic.h"


static bmpfont_t* font;

static uint32_t columns;
static uint32_t rows;
static uint32_t x0, y0;

static uint32_t color;
static uint32_t backcolor;
static point_t  cursor;

static bool_t  textInitialized;


static void __putchar__(bmpfont_t* __font, uint32_t __color, char __char, uint32_t __xoff, uint32_t __yoff) {
    char* _font_ptr = __font->_Buffer + (__char * __font->_Header->_CharSize);

    for (uint64_t _y = __yoff; _y < __yoff + 16; _y++) {
        for (uint64_t _x = __xoff; _x < __xoff + 8; _x++)
            if ((*_font_ptr & (0b10000000 >> (_x - __xoff))) > 0)
                kernel_kdd_pxcolor_set(_x, _y, __color);

        _font_ptr++;
    }
}

static void __newline__() {
    cursor.x  = x0;
    cursor.y += 16;

    SOFTASSERT(!(cursor.y < kernel_kdd_fbo_get()->_Height), RETVOID);

    kernel_text_scroll_up(1);
}

void kernel_text_scroll_up(uint32_t __lines) {
    SOFTASSERT(textInitialized, RETVOID);
    SOFTASSERT(__lines != 0, RETVOID);
    SOFTASSERT(__lines < rows, RETVOID);
    
    for (uint64_t y = __lines * 16; y < kernel_kdd_fbo_get()->_Height; y++) {
        for (uint64_t x = x0; x < kernel_kdd_fbo_get()->_PixelsPerScanLine; x++) {
            kernel_kdd_pxcolor_set(
                x, y - __lines * 16,
                kernel_kdd_pxcolor_get(x, y)
            );
        }
    }

    for (uint64_t y = cursor.y - (16 * __lines); y < kernel_kdd_fbo_get()->_Height; y++) {
        for (uint64_t x = x0; x < kernel_kdd_fbo_get()->_PixelsPerScanLine; x++) {
            kernel_kdd_pxcolor_set(
                x, y,
                backcolor
            );
        }
    }

    cursor.y -= 16 * __lines;
}

void kernel_text_scroll_down(uint32_t __lines) {
    SOFTASSERT(textInitialized, RETVOID);
    SOFTASSERT(__lines != 0, RETVOID);
    SOFTASSERT(__lines < rows, RETVOID);

    for (uint64_t y = __lines * 16; y < kernel_kdd_fbo_get()->_Height; y++) {
        for (uint64_t x = x0; x < kernel_kdd_fbo_get()->_PixelsPerScanLine; x++) {
            kernel_kdd_pxcolor_set(
                x, y + __lines * 16,
                kernel_kdd_pxcolor_get(x, y)
            );
        }
    }

    for (uint64_t y = 0; y < __lines * 16; y++) {
        for (uint64_t x = x0; x < kernel_kdd_fbo_get()->_PixelsPerScanLine; x++) {
            kernel_kdd_pxcolor_set(
                x, y,
                backcolor
            );
        }
    }
}

void kernel_text_clear_line(uint32_t __line) {
    SOFTASSERT(textInitialized, RETVOID);
}

void kernel_text_print_char(char __char) {
    SOFTASSERT(textInitialized, RETVOID);
    SOFTASSERT(__char != 0, RETVOID);

    if (__char == '\n') {
        __newline__();
        return;
    }

    if (cursor.x == kernel_kdd_fbo_get()->_PixelsPerScanLine)
        __newline__();
    
    __putchar__(
        font, color, __char,
        cursor.x, cursor.y
    );

    cursor.x += 8;
}

void kernel_text_print_str(char* __str) {
    SOFTASSERT(textInitialized, RETVOID);
    
    char* chr = __str;

    while (*chr != 0) {
        kernel_text_print_char(*chr);
        *chr++;
    }
}

void kernel_text_set_color(uint32_t __color, uint32_t __backcolor) {
    SOFTASSERT(textInitialized, RETVOID);
    
    if (__color != 0)     color     = __color;
    if (__backcolor != 0) backcolor = __backcolor; 
}

void kernel_text_initialize(uint32_t __color, uint32_t __backcolor, uint32_t __xoff, uint32_t __yoff, bmpfont_t* __font) {
    SOFTASSERT(!textInitialized, RETVOID);
    
    color     = __color;
    backcolor = __backcolor;
    x0        = __xoff;
    y0        = __yoff;
    font      = __font;

    cursor.x = x0;
    cursor.y = y0;

    columns = kernel_kdd_fbo_get()->_PixelsPerScanLine / 8;
    rows    = kernel_kdd_fbo_get()->_Height / 16;

    kernel_kdd_fbo_clear(backcolor);

    textInitialized = TRUE;
}

void kernel_text_reinitialize(uint32_t __color, uint32_t __backcolor, uint32_t __xoff, uint32_t __yoff) {
    SOFTASSERT(textInitialized, RETVOID);

    color     = __color;
    backcolor = __backcolor;
    x0        = __xoff;
    y0        = __yoff; 

    cursor.x  = x0;
    cursor.y  = y0;

    kernel_kdd_fbo_clear(backcolor);
}

void kernel_text_move_caret(int64_t __xoff, int64_t __yoff) {
    cursor.x += __xoff * 16;
    cursor.y += __yoff * 16;
}

void kernel_text_get_handles(uint32_t* __color, uint32_t* __backcolor, uint32_t* __xoff, uint32_t* __yoff, bmpfont_t** __font) {
    SOFTASSERT(textInitialized, RETVOID);
    
    ARGRET(__color, color);
    ARGRET(__backcolor, backcolor);
    
    ARGRET(__xoff, cursor.x);
    ARGRET(__yoff, cursor.y);
    ARGRET(__font, font);
}
