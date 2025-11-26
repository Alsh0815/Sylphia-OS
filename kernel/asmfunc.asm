bits 64
section .text

; void LoadCR3(uint64_t pml4_addr);
global LoadCR3
LoadCR3:
    mov cr3, rdi
    ret

; uint64_t GetCR3();
global GetCR3
GetCR3:
    mov rax, cr3
    ret

; void InvalidateTLB(uint64_t virtual_addr);
; ページテーブルを更新した際、CPUのキャッシュ(TLB)を更新するために必要
global InvalidateTLB
InvalidateTLB:
    invlpg [rdi]
    ret