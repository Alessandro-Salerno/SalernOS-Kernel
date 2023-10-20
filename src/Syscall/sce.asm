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

extern kernel_syscall_dispatch


global kernel_syscall_enable
kernel_syscall_enable:
    ; Enables SCE
    mov     rcx,    0xc0000080                  ; Use EFER MSR
    rdmsr                                       ; Read from EFER
    or      eax,    1                           ; Enable bit 0
    wrmsr                                       ; Write back

    ; Sets segments for user and kernel space
    mov     rcx,    0xc0000081                  ; Use STAR MSR
    rdmsr                                       ; Read from STAR
    mov     edx,    0x00180008                  ; Write segment offsets in STAR-Upper
    wrmsr                                       ; Write back

    ; Sets Syscall handler function
    push    rdx                                 ; Save RDX Register
    push    rax                                 ; Save RAX Register
    mov     rdx,    kernel_syscall_dispatch     ; Move handler address into RDX
    mov     rax,    rdx                         ; Copy RDX to RAX
    shr     rdx,    32                          ; Shift RDX by 32 bits to get the upper part of the address in EDX
    mov     rcx,    0xc0000082                  ; Use LSTAR MSR
    wrmsr                                       ; Write back
    pop     rax                                 ; Restore RAX
    pop     rdx                                 ; Restore RDX
    ret
