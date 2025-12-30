.global g_kernel_rsp_save
.section .bss
.align 16
g_kernel_rsp_save:
    .space 16

.text

/* void EnableSSE(); -> AArch64: Enable FP/ASIMD */
.global EnableSSE
EnableSSE:
    /* CPACR_EL1 (Architectural Feature Access Control Register) */
    /* FPEN (bits 20:21) = 0b11 (Trap nothing) */
    mrs x0, cpacr_el1
    orr x0, x0, #(3 << 20)
    msr cpacr_el1, x0
    isb
    ret

.global EnterUserMode
/* void EnterUserMode(uint64_t entry_point, uint64_t user_stack_top, int argc, uint64_t argv_ptr); */
/* x0 = entry_point, x1 = user_stack_top, x2 = argc, x3 = argv_ptr */
EnterUserMode:
    /* 割り込み禁止 (DAIF) はあえて操作しないか、必要ならマスクする */
    /* SPSR_EL1 (Saved Program Status Register) の設定 */
    /* EL0t (0b0000), DAIFマスクなし (0) -> 0x0 */
    mov x4, #0
    msr spsr_el1, x4

    /* ELR_EL1 (Exception Link Register) にエントリポイント設定 */
    msr elr_el1, x0

    /* SP_EL0 (User Stack Pointer) の設定 */
    msr sp_el0, x1

    /* カーネルスタックポインタを保存 */
    mov x4, sp
    adrp x5, g_kernel_rsp_save
    str x4, [x5, #:lo12:g_kernel_rsp_save]

    /* ユーザーモードへの引数渡し (AAPCS64: x0, x1) */
    /* x2(argc) -> x0, x3(argv) -> x1 */
    mov x0, x2
    mov x1, x3
    
    /* 念のため他のレジスタをクリア */
    mov x2, #0
    mov x3, #0
    mov x4, #0
    mov x5, #0
    
    /* EL0 へリターン */
    eret

.global ExitApp
ExitApp:
    /* 現状の実装ではSVCハンドラ経由で戻るべきだが、
       関数の形として用意しておく場合のプレースホルダー */
    /* カーネルスタック復元などはSVCハンドラ内で行う */
    b .

/* void LoadCR3(uint64_t pml4_addr); -> TTBR0_EL1 */
.global LoadCR3
LoadCR3:
    msr ttbr0_el1, x0
    isb
    ret

/* AArch64はフラットメモリモデルなのでGDT/TRは不要 */
.global LoadGDT
LoadGDT:
    ret

.global LoadTR
LoadTR:
    ret

/* uint64_t GetCR2(); -> FAR_EL1 (Fault Address Register) */
.global GetCR2
GetCR2:
    mrs x0, far_el1
    ret

/* uint64_t GetCR3(); -> TTBR0_EL1 */
.global GetCR3
GetCR3:
    mrs x0, ttbr0_el1
    ret

/* void InvalidateTLB(uint64_t virtual_addr); */
.global InvalidateTLB
InvalidateTLB:
    /* x0 = Virtual Address */
    /* TLBI VAE1, Xt : Invalidate by VA, EL1 */
    /* アドレスはページ番号(ASID含むかも)形式にする必要があるが、
       詳細略で全リア(vmalle1)を使う手もある。
       ここでは単純化のため ALLE1 (All EL1) を使う */
    tlbi vmalle1
    dsb ish
    isb
    ret

/* void SetDSAll(uint16_t value); -> 不要 */
.global SetDSAll
SetDSAll:
    ret

/* ReadMSR/WriteMSR: 汎用的な命令はないため、ダミー実装 */
.global ReadMSR
ReadMSR:
    mov x0, #0
    ret

.global WriteMSR
WriteMSR:
    ret

/* Syscall Handler */
.extern SyscallHandler
.global SyscallEntry
SyscallEntry:
    /* SVC割り込みからここに来る想定 (Vector Table設定が必要) */
    /* これ単体では動作しないが、シンボル定義用 */
    b .
