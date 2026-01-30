; SalernOS Kernel
; Copyright (C) 2021 - 2026 Alessandro Salerno
; 
; This program is free software: you can redistribute it and/or modify
; it under the terms of the GNU General Public License as published by
; the Free Software Foundation, either version 3 of the License, or
; (at your option) any later version.
; 
; This program is distributed in the hope that it will be useful,
; but WITHOUT ANY WARRANTY; without even the implied warranty of
; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
; GNU General Public License for more details.
; 
; You should have received a copy of the GNU General Public License
; along with this program.  If not, see <https://www.gnu.org/licenses/>.

section .text

; CREDIT: Mathewnd/Astral
; on entry, interrupts are disabled automatically by SCE
; registers:
;   rcx = user rip
;   r11 = user rflags
;   
;   rax = syscall number
;   rdi = arg1
;   rsi = arg2
;   rdx = arg3
;   r10 = arg4
;   r8  = arg5
;   r9  = arg6
global x86_64_syscall_sce_entry
x86_64_syscall_sce_entry:
    swapgs
	; saving the syscall number on cr2 is cursed but we need this extra register
	; and taking a page fault here would result in a triple fault anyways
	; because it's still using the user stack
	mov  cr2, rax
	; rax can be used just fine now
	mov  rax, [gs:0] ; thread pointer
	mov  rax, [rax] ; kernel stack top
	xchg rsp, rax ; switch stack pointers

    ; interrupts push some things automatically on the satack, so we do it
    ; manually here
    push qword 0x1b ; user ss
    push rax        ; user rsp
    push r11        ; user rflags
    push qword 0x23 ; user cs
    push rcx        ; user rip
    push qword 0    ; error = 0
    push rbp        ; user rbp (not swapped before)

    ; then we push almost the same things as interrupts
    push  rsi
    push  rdi
    push  r15
    push  r14
    push  r13
    push  r12
    push  qword 0 ; r11 holds user rflags so we push it there
    push  r10
    push  r9
    push  r8
    push  rdx
    push  qword 0 ; rcx holds user rip so we push it there
    push  rbx
    mov   rax, cr2
    push  rax     ; before we moved user rax in cr2
    mov   rax, ds
    push  rax
    mov   rax, es
    push  rax
    mov   rax, fs
    push  rax
    mov   rax, gs
    push  rax	
    push  qword 0  ; do not push cr2 (overridden)

    ; Set kernel segments
    ; NOTE: 0x10 is the GDT selector for kernel data
    mov   rdi, 0x10
    mov   ds, rdi
    mov   es, rdi

    mov    rdi, rsp  ; pass context pointer to handler
    extern x86_64_syscall_handle_sce
    call   x86_64_syscall_handle_sce

    ; Pop context from stack
    add   rsp, 24 ; cr2, gs, and fs are not popped
    pop   rax
    mov   es, rax
    pop   rax
    mov   ds, rax
    pop   rax
    pop   rbx
    add   rsp, 8  ; do not pop rcx (0)
    pop   rdx
    pop   r8
    pop   r9
    pop   r10
    add   rsp, 8  ; do not pop r11
    pop   r12
    pop   r13
    pop   r14
    pop   r15
    pop   rdi
    pop   rsi

    ; now, deconstruct the part of the frame which simulates the interrupt stack
    ; (pushed at line 39)
    pop   rbp
    add   rsp, 8  ; do not pop error
    pop   rcx     ; sysret restores rip from rcx
    add   rsp, 8  ; do not pop user cs
    pop   r11     ; sysret restore rfalgs from r11
    pop   rsp     ; restore user stack

    swapgs
    o64 sysret
