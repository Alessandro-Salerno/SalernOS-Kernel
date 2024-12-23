section .text

global _start
_start:
  mov rax, qword 3
  mov rdi, qword path
  mov rsi, qword argv
  mov rdx, qword envp
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

arg1: db "ciao", 0
arg2: db "test", 0
argv: dq arg1, arg2, 0
env1: db "TERM=linux", 0
envp: dq env1, 0
