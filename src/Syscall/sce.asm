; SalernOS Kernel
; Copyright (C) 2021 - 2022 Alessandro Salerno

; This program is free software; you can redistribute it and/or modify
; it under the terms of the GNU General Public License as published by
; the Free Software Foundation; either version 2 of the License, or
; (at your option) any later version.

; This program is distributed in the hope that it will be useful,
; but WITHOUT ANY WARRANTY; without even the implied warranty of
; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
; GNU General Public License for more details.

; You should have received a copy of the GNU General Public License along
; with this program; if not, write to the Free Software Foundation, Inc.,
; 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.


[bits 64]
kernel_syscall_enable:
    mov     rcx,    0xc0000080
    rdmsr
    or      eax,    1
    wrmsr
    mov     rcx,    0xc0000081
    rdmsr
    mov     edx,    0x00180008
    wrmsr
    ret


GLOBAL kernel_syscall_enable