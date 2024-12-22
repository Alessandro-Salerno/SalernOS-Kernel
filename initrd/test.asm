section .text

global _start
_start:
  mov rax, qword 1
  mov rdi, qword 0
  mov rsi, qword hello
  mov rdx, qword 12
  int 80h
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
.nothing:
  nop
  jmp .nothing

section .data
buffer: times 20 db 0

section .rodata
hello:
  db "Hello, world", 0
