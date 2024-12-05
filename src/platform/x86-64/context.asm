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

