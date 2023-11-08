// NOTE: This file is a redistribution of Lyre/kernel/src/sys/int_thunks.S
// The original code is distributed under the terms of the BSD 2-Clause license
// License and copyright notice for the original Lyre source are included
// Changes made as part of the SalernOS-Kernel are distributed under the GNU GPL v2 license
// License and copyright notice for SalernOS-Kernel source are included


/*
 * Original Lyre code:
 * Copyright (C) 2022-2023 mintsuki and contributors.
 * 
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* 
 * SalernOS Kernel
 * Copyright (C) 2021 - 2023 Alessandro Salerno
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */


.altmacro

.macro THUNK vec
__thunk__\vec:
  .if \vec != 8 && \vec != 10 && \vec != 11 && \vec != 12 && \vec != 13 && \vec != 14 && \vec != 17 && \vec != 30
    push $0
  .endif

  cmpq $0x4b, 16(%rsp)
  jne 1f
  swapgs

1:
  push %r15
  push %r14
  push %r13
  push %r12
  push %r11
  push %r10
  push %r9
  push %r8
  push %rbp
  push %rdi
  push %rsi
  push %rdx
  push %rcx
  push %rbx
  push %rax
  mov %es, %eax
  push %rax
  mov %ds, %eax
  push %rax

  cld

  mov $0x30, %eax
  mov %eax, %ds
  mov %eax, %es
  mov %eax, %ss

  mov $\vec, %rdi
  mov $(\vec * 8), %rax
  lea isrLookup(%rip), %rbx
  add %rax, %rbx
  mov %rsp, %rsi
  xor %rbp, %rbp
  call *(%rbx)

  pop %rax
  mov %eax, %ds
  pop %rax
  mov %eax, %es
  pop %rax
  pop %rbx
  pop %rcx
  pop %rdx
  pop %rsi
  pop %rdi
  pop %rbp
  pop %r8
  pop %r9
  pop %r10
  pop %r11
  pop %r12
  pop %r13
  pop %r14
  pop %r15
  add $8, %rsp

  cmpq $0x4b, 8(%rsp)
  jne 1f
  swapgs

1:
  iretq
.endm

.macro THUNKADDR vec
  .quad __thunk__\vec
.endm


.section .data

.global isrThunkLookup
.align 8
isrThunkLookup:
  .set i, 0
  .rept 256
    THUNKADDR %i
    .set i, i + 1
  .endr


.section .text

.set i, 0
.rept 256
  THUNK %i
  .set i, i + 1
.endr

.global sys_idt_ipi_entry_panic
sys_idt_ipi_entry_panic:

