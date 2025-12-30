.global _start
.extern main

.text

_start:
    and $-16, %rsp
    call main
    mov $2, %rax
    syscall

.hang:
    jmp .hang
