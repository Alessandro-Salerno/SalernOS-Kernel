; SalernOS Kernel
; Copyright (C) 2021 - 2025 Alessandro Salerno
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

%assign i 0
%rep 256
x86_64_isr_%+i:
; Push a value to the stack in order to follow
; the guidelines of the Intel SDM on ISR stack contents
%if i <> 8 && i <> 10 && i <> 11 && i <> 12 && i <> 13 && i <> 14
  push  qword 0
%endif

  push  rbp
  mov   rbp, i
  jmp   x86_64_isr_common
%assign i i+1
%endrep

x86_64_isr_common:
  ; Check if SWAPGS should be called.
  ; Following the x86-64 architecture spec, the CS register
  ; should be found 24 bytes above the current stack pointer.
  ; The CS register holds the current GDT segment selector in the
  ; left-most bits, and the CPL in the lower bits.
  ; In SalernOS, the GDT selector for User Code is 0x20, and the CPL
  ; is 3, thus to check if SWAPGS should be called, CS should be compared
  ; against 0x23 (0x20 | 0x03)
  cmp   qword [rsp+24], 0x23
  jne   .noswapgs
  swapgs
.noswapgs:
  ; Push registers to the stack
  ; NOTE: some registers are moved into rax or
  ;       other general-purpose registers because
  ;       they cannot be pushed directly
  push  rsi
  push  rdi
  push  r15
  push  r14
  push  r13
  push  r12
  push  r11
  push  r10
  push  r9
  push  r8
  push  rdx
  push  rcx
  push  rbx
  push  rax
  mov   rax, ds
  push  rax
  mov   rax, es
  push  rax
  mov   rax, fs
  push  rax
  mov   rax, gs
  push  rax	
  mov   rdi, cr2
  push  rdi

  ; Set kernel segments
  ; NOTE: 0x10 is the GDT selector for kernel data
  mov   rdi, 0x10
  mov   ds, rdi
  mov   es, rdi

  mov   rdi, rbp  ; Save interrupt vector
  mov   rsi, rsp  ; Save interrupt context

  cld
  extern com_sys_interrupt_isr
  call  com_sys_interrupt_isr
  cli

  ; Pop context from stack
  add   rsp, 24 ; cr2, gs, and fs are not popped
  pop   rax
  mov   es, rax
  pop   rax
  mov   ds, rax
  pop   rax
  pop   rbx
  pop   rcx
  pop   rdx
  pop   r8
  pop   r9
  pop   r10
  pop   r11
  pop   r12
  pop   r13
  pop   r14
  pop   r15
  pop   rdi
  pop   rsi
  pop   rbp
  add   rsp, 8 ; remove error code

  ; Check again if SWAPGS is needed
  ; NOTE: The procedure is described above
  cmp   qword [rsp+8], 0x23
  jne   .noswapgs2
  swapgs
.noswapgs2:
  o64   iret

section .rodata
global IsrTable
IsrTable:
%assign i 0
%rep 256
  dq x86_64_isr_%+i
%assign i i+1
%endrep
