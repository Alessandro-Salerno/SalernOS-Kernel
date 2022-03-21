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


#include "Memory/Heap/heap.h"
#include <kmalloc.h>
#include <kmem.h>


void* kmalloc(size_t __buffsize) {
    return kernel_heap_allocate(__buffsize);
}

void* krealloc(void* __buff, size_t __oldsize, size_t __newsize) {
    void* _newbuff = kernel_heap_allocate(__newsize);
    kmemcpy(__buff, _newbuff, __oldsize);

    kernel_heap_free(__buff);
    return _newbuff;
}

void kfree(void* __buff) {
    kernel_heap_free(__buff);
}
