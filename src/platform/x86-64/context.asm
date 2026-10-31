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

global x86_64_ctx_switch
x86_64_ctx_switch:
  ; rdi: hidden result pointer, rsi: to, rdx: from, rcx/r8/r9: message
  push rdi
  mov qword [rdx + 48], rbx
  mov qword [rdx + 152], rbp
  mov qword [rdx + 104], r12
  mov qword [rdx + 112], r13
  mov qword [rdx + 120], r14
  mov qword [rdx + 128], r15
  mov qword [rdx + 192], rsp

  mov rbx, qword [rsi + 48]
  mov rbp, qword [rsi + 152]
  mov r12, qword [rsi + 104]
  mov r13, qword [rsi + 112]
  mov r14, qword [rsi + 120]
  mov r15, qword [rsi + 128]
  mov rsp, qword [rsi + 192]

  pop rax
  mov [rax], rcx
  mov [rax + 8], r8
  mov [rax + 16], r9
  ret

global arch_context_trampoline
arch_context_trampoline:
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

global x86_64_ctx_test_trampoline
x86_64_ctx_test_trampoline:
  mov rdi, rsp
  jmp arch_context_trampoline

global arch_context_fork_trampoline
arch_context_fork_trampoline:
  ; Fresh threads have no suspended C caller. Enter its C equivalent.
  mov rdi, rax
  mov rsi, [rax + 24]
  sub rsp, 8
  extern x86_64_ctx_first_entry
  jmp x86_64_ctx_first_entry
