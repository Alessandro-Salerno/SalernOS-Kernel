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


#include <kmath.h>


uint64_t kabs(int64_t __val) {
    return (__val < 0) ? __val * -1 : __val;
}

uint64_t kroundl(uint64_t __val, uint64_t __round) {
    uint64_t _rem = __val % __round;
    return (_rem > 0) ? __val - _rem + __round : __val;
}