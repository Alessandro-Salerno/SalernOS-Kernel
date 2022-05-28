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


#include <User/Output/Text/textrenderer.h>
#include <kstdio.h>
#include "kdebug.h"

#define INFO_FGCOLOR 0xffffffff
#define INFO_BGCOLOR 0xff000000
#define INFO_TEXT    "[  DEBUG  ]"

#define ERR_FGCOLOR  0xffffffff
#define ERR_BGCOLOR  0xffff0000
#define ERR_TEXT     "[  ERROR  ]"

#define WARN_FGCOLOR 0xff000000
#define WARN_BGCOLOR 0xffffff00
#define WARN_TEXT    "[ WARNING ]"

#define OK_FGCOLOR   0xff000000
#define OK_BGCOLOR   0xff00ff00
#define OK_TEXT      "[ SUCCESS ]"


static void __putmsg__(uint32_t __color, uint32_t __backcolor, const char* __type, const char* __msg) {
    uint32_t _color,
             _backcolor;

    point_t _pos;

    arch_boot_text_info_get(&_color, &_backcolor, &_pos, NULL, NULL);
    arch_boot_text_info_set(__color, __backcolor, _pos);

    kprintf("%s\t%s", __type, __msg);

    arch_boot_text_info_get(NULL, NULL, &_pos, NULL, NULL);
    arch_boot_text_info_set(_color, _backcolor, _pos);
    kprintf("\n");
}

void kloginfo(const char* __msg) {
    __putmsg__(
        INFO_FGCOLOR, INFO_BGCOLOR,
        INFO_TEXT, __msg
    );
}

void klogerr(const char* __msg) {
    __putmsg__(
        ERR_FGCOLOR, ERR_BGCOLOR,
        ERR_TEXT, __msg
    );
}

void klogwarn(const char* __msg) {
    __putmsg__(
        WARN_FGCOLOR, WARN_BGCOLOR,
        WARN_TEXT, __msg
    );
}

void klogok(const char* __msg) {
    __putmsg__(
        OK_FGCOLOR, OK_BGCOLOR,
        OK_TEXT, __msg
    );
}
