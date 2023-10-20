; SalernOS Kernel
; Copyright (C) 2021 - 2023 Alessandro Salerno

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


global kernel_gdt_load
kernel_gdt_load:
    lgdt    [rdi]
    mov     ax,     0x30
    ltr     ax
    mov     ax,     0x10
    mov     ds,     ax
    mov     es,     ax
    mov     fs,     ax
    mov     gs,     ax
    mov     ss,     ax

    pop     rdi
    mov     rax,    0x08
    push    rax
    push    rdi
    retfq
