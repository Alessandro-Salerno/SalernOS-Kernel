; SalernOS Kernel
; Copyright (C) 2021 - 2024 Alessandro Salerno
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

global x86_64_ctx_switch
x86_64_ctx_switch:
  mov qword [rsi + 48], rbx
  mov qword [rsi + 152], rbp
  mov qword [rsi + 104], r12
  mov qword [rsi + 112], r13
  mov qword [rsi + 120], r14
  mov qword [rsi + 128], r15
  mov qword [rsi + 192], rsp

  mov rbx, qword [rdi + 48]
  mov rbp, qword [rdi + 152]
  mov r12, qword [rdi + 104]
  mov r13, qword [rdi + 112]
  mov r14, qword [rdi + 120]
  mov r15, qword [rdi + 128]
  mov rsp, qword [rdi + 192]

  ret

global arch_ctx_trampoline
arch_ctx_trampoline:
	; rdi is the pointer to the context struct
  cli
	mov   rsp, rdi

	add   rsp, 24 ; cr2 gs and fs are not popped.
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

	; check if swapgs is needed
	cmp   qword [rsp+8], 0x23
	jne   .noswapgs
	swapgs
	.noswapgs:
	o64 iret

