section .text

global _start
_start:
  mov rax, qword 1
  mov rdi, qword 0
  mov rsi, qword hello
  mov rdx, qword 12
  int 80h
.nothing:
  nop
  jmp .nothing

section .rodata
hello:
  db "Hello, world", 0
