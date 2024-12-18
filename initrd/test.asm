section .text

global _start
_start:
  mov rax, qword 0
  mov rdi, qword hello
  int 80h
.nothing:
  nop
  jmp .nothing

section .rodata
hello:
  db "Hello, world", 0
