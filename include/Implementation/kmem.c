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


#include <kmem.h>

#define KMEMASSERT(__cond) if (!(__cond)) return;


void kmemset(void* __buff, uint64_t __buffsize, uint8_t __val) {
    KMEMASSERT(__buffsize >= 16);

    __uint128_t _128bit_val = __val;
    for (uint8_t _i = 8; _i < 128; _i += 8)
        _128bit_val |= (__uint128_t)(__val) << _i;

    for (uint64_t _i = 0; _i < __buffsize; _i += 16)
        *(__uint128_t*)((uint64_t)(__buff) + _i) = _128bit_val;

    for (uint64_t _i = __buffsize - (__buffsize % 16); _i < __buffsize; _i++)
        *(uint8_t*)((uint64_t)(__buff) + _i) = _128bit_val;
}

bool_t  kmemcmp(void* __buff1, void* __buff2, uint64_t __buffsize) {
    for (uint64_t _i = 0; _i < __buffsize; _i++) {
        uint8_t _buff1val = *(uint8_t*)(__buff1 + _i);
        uint8_t _buff2val = *(uint8_t*)(__buff2 + _i);

        if (_buff1val != _buff2val) return FALSE;
    }

    return TRUE;
}

void kmemcpy(void* __from, void* __to, size_t __buffsize) {
    for (uint64_t _i = 0; _i < __buffsize; _i++) {
        uint8_t _val = *(uint8_t*)(__from + _i);
        *(uint8_t*)((uint64_t)(__to) + _i) = _val;
    }
}
