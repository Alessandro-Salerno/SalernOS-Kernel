section .data
hello:
  db "Hello, world", 0

section .text

global _start
_start:
  mov rax, 0
  mov rdi, [qword hello]
  int 80h
