section .text

global _start
_start:
  mov rax, qword 4
  int 80h
  nop
  jmp _start

section .data
buffer: times 20 db 0

section .rodata
hello:
  db "Hello, world", 0

path:
  db "./test", 0
