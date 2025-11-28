bits 64
global _start
extern main

section .text
_start:
    call main
    mov rax, 2 ; Syscall Exit
    syscall

.hang:
    jmp .hang