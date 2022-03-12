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


#include "Memory/bmp.h"


bool_t kernel_bmp_get(bmp_t* __bmp, uint64_t __idx) {
    uint64_t _byte_idx    = __idx / 8;
    uint8_t  _bit_idx     = __idx % 8;
    uint8_t  _bit_indexer = 0b10000000 >> _bit_idx;

    return (__bmp->_Buffer[_byte_idx] & _bit_indexer) > 0;
}

void kernel_bmp_set(bmp_t* __bmp, uint64_t __idx, bool_t __val) {
    uint64_t _byte_idx    = __idx / 8;
    uint8_t  _bit_idx     = __idx % 8;
    uint8_t  _bit_indexer = 0b10000000 >> _bit_idx;
    
    __bmp->_Buffer[_byte_idx] &= ~_bit_indexer;
    __bmp->_Buffer[_byte_idx] |=  _bit_indexer * __val;
}
