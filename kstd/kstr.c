/**********************************************************************
SalernOS Kernel
Copyright (C) 2021 - 2023 Alessandro Salerno

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


#include <kstr.h>


static char kstrOutput[24];


const char* uitoa(uint64_t __val) {
    uint64_t _size_test = 10;
    uint8_t  _size      = 0;
    uint8_t  _idx       = 0;

    while (__val / _size_test > 0) {
        _size_test *= 10;
        _size++;
    }

    while (_idx < _size) {
        uint8_t _rem              = __val % 10;
        __val                    /= 10;
        kstrOutput[_size - _idx] = _rem + '0';
        _idx++;
    }

    uint8_t _rem              = __val % 10;
    __val                    /= 10;
    kstrOutput[_size - _idx] = _rem + '0';
    kstrOutput[_size + 1]    = 0;
    
    return kstrOutput;
}

const char* itoa(int64_t __val) {
    uint64_t _size_test   = 10;
    uint8_t  _size        = 0;
    uint8_t  _idx         = 0;
    uint8_t  _is_negative = 0;

    _is_negative = __val < 0;
    kstrOutput[0] = (__val < 0) ? '-' : '+';
    __val        = kabs(__val);

    while (__val / _size_test > 0) {
        _size_test *= 10;
        _size++;
    }

    while (_idx < _size) {
        uint8_t _rem                             = __val % 10;
        __val                                   /= 10;
        kstrOutput[_is_negative + _size - _idx]  = _rem + '0';
        _idx++;
    }

    uint8_t _rem                             = __val % 10;
    __val                                   /= 10;
    kstrOutput[_is_negative + _size - _idx]  = _rem + '0';
    kstrOutput[_is_negative + _size + 1]     = 0;
    
    return kstrOutput;
}
