/**********************************************************************
SalernOS Kernel
Copyright (C) 2021 - 2022 Alessandro Salerno

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
**********************************************************************/


#include "User/Output/Text/textrenderer.h"
#include "kernelpanic.h"


static bmpfont_t* font;

static uint32_t numColumns;
static uint32_t numRows;

static uint32_t foregroundColor;
static uint32_t backgroundColor;

static point_t startPosition;
static point_t endPosition;
static point_t lastPosition;
static point_t curPosition;

static bool  trendrInitialized;


static void __initialize__(uint32_t __color, uint32_t __backcolor, uint64_t __xoff, uint64_t __yoff) {
    uint64_t _buff_w = kernel_kdd_fbo_get()->_Width;
    uint64_t _buff_h = kernel_kdd_fbo_get()->_Height;

    foregroundColor = __color;
    backgroundColor = __backcolor;
    
    startPosition = (point_t) {
        .x = __xoff,
        .y = __yoff
    };

    endPosition = (point_t) {
        .x = _buff_w - (_buff_w %  8),
        .y = _buff_h - (_buff_h % 16)
    };

    curPosition  = startPosition;
    lastPosition = curPosition;

    numColumns = endPosition.x / 8;
    numRows    = endPosition.y / 16;

    kernel_kdd_fbo_clear(backgroundColor);
}

static void __drawchar__(char __char) {
    char* _font_ptr = font->_Buffer + (__char * font->_Header->_CharSize);

    uint64_t _start_x = curPosition.x;
    uint64_t _start_y = curPosition.y;

    uint64_t _end_x = _start_x + 8,
             _end_y = _start_y + 16;

    for (uint64_t _y = _start_y; _y < _end_y; _y++) {
        for (uint64_t _x = _start_x; _x < _end_x; _x++) {
            kernel_kdd_pxcolor_set(
                _x, _y, 
                ((*_font_ptr & ((1 << 7) >> (_x - _start_x))))
                    ? foregroundColor : backgroundColor
            );
        }

        _font_ptr++;
    }
}

static void __newline__() {
    curPosition.x  = startPosition.x;
    curPosition.y += 16;

    SOFTASSERT(!(curPosition.y < endPosition.y), RETVOID);

    kernel_text_scroll(1);
}

static void __putch__(char __char) {
    lastPosition = curPosition;

    switch (__char) {
        case '\n': kernel_text_blitch(' ');   __newline__();    return;
        case '\t': kernel_text_print("    ");                   return;
    }

    kernel_text_blitch(__char);
    curPosition.x += 8;
}


void kernel_text_scroll(uint32_t __lines) {
    SOFTASSERT(trendrInitialized, RETVOID);
    SOFTASSERT(__lines != 0, RETVOID);
    SOFTASSERT(__lines < numRows, RETVOID);
    
    for (uint64_t _y = __lines * 16; _y < endPosition.y; _y++) {
        for (uint64_t _x = startPosition.x; _x < kernel_kdd_fbo_get()->_PixelsPerScanLine; _x++) {
            kernel_kdd_pxcolor_set(
                _x, _y - __lines * 16,
                kernel_kdd_pxcolor_get(_x, _y)
            );
        }
    }

    for (uint64_t _l = 0; _l < __lines; _l++)
        kernel_text_line_clear(curPosition.y / 16 - _l);

    curPosition.y -= 16 * __lines;
}

void kernel_text_line_clear(uint32_t __line) {
    SOFTASSERT(trendrInitialized, RETVOID);

    for (uint64_t _y = __line * 16 - 16; _y < __line * 16; _y++) {
        for (uint64_t _x = startPosition.x; _x < endPosition.x; _x++) {
            kernel_kdd_pxcolor_set(
                _x, _y,
                backgroundColor
            );
        }
    }
}

void kernel_text_blitch(char __char) {
    SOFTASSERT(trendrInitialized, RETVOID);
    SOFTASSERT(__char != 0, RETVOID);
    
    if (curPosition.x == endPosition.x)
        __newline__();
    
    __drawchar__(__char);
}

void kernel_text_putch(char __char) {
    SOFTASSERT(trendrInitialized, RETVOID);
    SOFTASSERT(__char != 0, RETVOID);

    __putch__(__char);
    kernel_text_blitch(CURSOR_CHARACTER);
}

void kernel_text_print(char* __str) {
    SOFTASSERT(trendrInitialized, RETVOID);

    char* _chr = __str;

    while (*_chr) {
        kernel_text_putch(*_chr);
        *_chr++;
    }
}

void kernel_text_info_set(uint32_t __color, uint32_t __backcolor, point_t __pos) {
    SOFTASSERT(trendrInitialized, RETVOID);
    
    foregroundColor = __color;
    backgroundColor = __backcolor;

    curPosition     = __pos;
    lastPosition    = curPosition;
}

void kernel_text_initialize(uint32_t __color, uint32_t __backcolor, uint32_t __xoff, uint32_t __yoff, bmpfont_t* __font) {
    SOFTASSERT(!trendrInitialized, RETVOID);

    __initialize__(__color, __backcolor, __xoff, __yoff);
    font = __font;

    trendrInitialized = TRUE;
}

void kernel_text_reinitialize(uint32_t __color, uint32_t __backcolor, uint32_t __xoff, uint32_t __yoff) {
    SOFTASSERT(trendrInitialized, RETVOID);
    __initialize__(__color, __backcolor, __xoff, __yoff);
}

void kernel_text_info_get(uint32_t* __color, uint32_t* __backcolor, point_t* __curpos, point_t* __lpos, bmpfont_t** __font) {
    SOFTASSERT(trendrInitialized, RETVOID);

    ARGRET(__color, foregroundColor);
    ARGRET(__backcolor, backgroundColor);

    ARGRET(__curpos, curPosition);
    ARGRET(__lpos, lastPosition);
    ARGRET(__font, font);
}
