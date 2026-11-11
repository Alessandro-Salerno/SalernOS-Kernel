section .text

global ctx_switch
ctx_switch:
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

global ctx_save_and_call
ctx_save_and_call:
  ; rdi: fn(ctx, arg), rsi: arg
  ; The pushed context_t resumes our caller as if we had returned
  mov  rax, [rsp]
  lea  rcx, [rsp + 8]
  push qword 0x10 ; ss
  push rcx        ; rsp
  pushfq
  push qword 0x08 ; cs
  push rax        ; rip
  push qword 0    ; error
  push rbp
  push rsi
  push rdi
  push r15
  push r14
  push r13
  push r12
  push r11
  push r10
  push r9
  push r8
  push rdx
  push rcx
  push rbx
  push qword 0    ; rax: return value
  push qword 0x10 ; ds
  push qword 0x10 ; es
  push qword 0    ; fs
  push qword 0    ; gs
  push qword 0    ; cr2

  mov  rax, rdi
  mov  rdi, rsp
  sub  rsp, 8 ; 208-byte frame + return address: realign to 16
  call rax
  add  rsp, 208 + 8
  xor  eax, eax
  ret

global context_trampoline
context_trampoline:
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

global ctx_test_trampoline
ctx_test_trampoline:
  mov rdi, rsp
  jmp context_trampoline

global context_fork_trampoline
context_fork_trampoline:
  ; Fresh threads have no suspended C caller. Enter its C equivalent.
  mov rdi, rax
  mov rsi, [rax + 24]
  sub rsp, 8
  extern ctx_first_entry
  jmp ctx_first_entry
