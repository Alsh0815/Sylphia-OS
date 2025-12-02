bits 64
global _start
extern main

section .text

_start:
    and rsp, -16
    call main
    mov rax, 2
    syscall

.hang:
    jmp .hang