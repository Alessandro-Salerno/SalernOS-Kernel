section .text

global _start
_start:
  mov rax, qword 2
  mov rdi, qword 1
  mov rsi, qword buffer
  mov rdx, qword 20
  int 80h
  mov rax, qword 1
  mov rdi, qword 0
  mov rsi, qword buffer
  mov rdx, qword 20
  int 80h
  mov rax, qword 3
  mov rdi, qword path
  int 80h

section .data
buffer: times 20 db 0

section .rodata
hello:
  db "Hello, world", 0

path:
  db "./test", 0
