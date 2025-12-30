.global g_kernel_rsp_save
.section .bss
.align 8
g_kernel_rsp_save:
    .space 8

.text

/* void EnableSSE(); */
.global EnableSSE
EnableSSE:
    /* CR0 読み込み */
    mov %cr0, %rax
    /* EM (Emulation) ビット (bit 2) をクリア (0) */
    and $0xFFFB, %ax
    /* MP (Monitor Coprocessor) ビット (bit 1) をセット (1) */
    or $0x0002, %ax
    mov %rax, %cr0
    /* CR4 読み込み */
    mov %cr4, %rax
    /* OSFXSR (OS Support for FXSAVE/FXRSTOR) ビット (bit 9) をセット (1) */
    or $(3 << 9), %ax
    mov %rax, %cr4
    ret

.global EnterUserMode

/* void EnterUserMode(uint64_t entry_point, uint64_t user_stack_top, int argc, uint64_t argv_ptr); */
EnterUserMode:
    push %rbp
    push %rbx
    push %r12
    push %r13
    push %r14
    push %r15
    mov %rsp, g_kernel_rsp_save(%rip)
    /* System V ABI: RDI = entry_point, RSI = user_stack_top, RDX = argc, RCX = argv */
    cli                 /* 割り込み禁止（コンテキストスイッチ中） */
    /* 1. SS (User Data Segment) */
    mov $0x23, %rax
    push %rax
    /* 2. RSP (User Stack Pointer) */
    push %rsi
    /* 3. RFLAGS */
    pushf
    pop %rax
    or $0x200, %rax
    push %rax
    /* 4. CS (User Code Segment) */
    mov $0x2B, %rax
    push %rax
    /* 5. RIP (Entry Point) */
    push %rdi

    /* Setup Arguments for User Mode (ELF expects System V ABI) */
    /* User (SysV):   RDI=argc, RSI=argv */
    mov %rdx, %rdi /* argc */
    mov %rcx, %rsi /* argv */
    
    /* セグメントレジスタの初期化 (DS, ES, FS, GS) */
    mov $0x23, %ax  /* User Data Segment */
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    /* swapgsでGS_BASEとKERNEL_GS_BASEを入れ替える */
    /* syscall時に再度swapgsすると、GS_BASEがg_syscall_contextを指すようになる */
    iretq

.global ExitApp
ExitApp:
    mov g_kernel_rsp_save(%rip), %rsp
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %rbx
    pop %rbp
    /* swapgs */
    sti                 /* 割り込みを有効化 */
    ret

/* void LoadCR3(uint64_t pml4_addr); */
.global LoadCR3
LoadCR3:
    mov %rdi, %cr3
    ret

/* void LoadGDT(uint16_t limit, uint64_t offset); */
.global LoadGDT
LoadGDT:
    push %rbp
    mov %rsp, %rbp
    sub $16, %rsp         /* スタック上にGDT記述子用の領域を確保 (10バイト必要だがアライメントのため16) */

    mov %di, (%rsp)       /* Limit (16bit) をセット */
    mov %rsi, 2(%rsp)     /* Base (64bit) をセット */
    lgdt (%rsp)          /* GDTをロード */

    /* GDTをロードしただけではCSレジスタのキャッシュが古いままなので、 */
    /* Far Return (retfq) を使って強制的に CS を 0x08 (Kernel Code) にリロードする */
    
    push $0x08           /* 新しいCS (Kernel Code Segment) */
    lea .reload_cs(%rip), %rax
    push %rax            /* 戻り先アドレス (RIP) */
    lretq               /* [CS] [RIP] をポップしてジャンプ */

.reload_cs:
    mov %rbp, %rsp
    pop %rbp
    ret

/* void LoadTR(uint16_t sel); */
.global LoadTR
LoadTR:
    ltr %di
    ret

/* uint64_t GetCR2(); */
.global GetCR2
GetCR2:
    mov %cr2, %rax
    ret

/* uint64_t GetCR3(); */
.global GetCR3
GetCR3:
    mov %cr3, %rax
    ret

/* void InvalidateTLB(uint64_t virtual_addr); */
/* ページテーブルを更新した際、CPUのキャッシュ(TLB)を更新するために必要 */
.global InvalidateTLB
InvalidateTLB:
    invlpg (%rdi)
    ret

/* void SetDSAll(uint16_t value); */
.global SetDSAll
SetDSAll:
    mov %di, %ds
    mov %di, %es
    mov %di, %fs
    mov %di, %gs
    mov %di, %ss          /* SSも一緒に設定 */
    ret



/* uint64_t ReadMSR(uint32_t msr); */
.global ReadMSR
ReadMSR:
    /* 引数: RDI = msr番号 */
    /* rdmsr命令: ECXにMSR番号を入れて実行 -> EDX:EAXに値が返る */
    mov %rdi, %rcx
    rdmsr
    /* EDX:EAX を 64bitの RAX にまとめる (戻り値) */
    shl $32, %rdx
    or %rdx, %rax
    ret

/* void WriteMSR(uint32_t msr, uint64_t value); */
.global WriteMSR
WriteMSR:
    /* 引数: RDI = msr番号, RSI = 書き込む値 (64bit) */
    /* wrmsr命令: ECXにMSR番号, EDX:EAXに値(64bit)を入れて実行する */
    mov %rdi, %rcx    /* MSR番号 */
    mov %esi, %eax    /* 値の下位32bit */
    mov %rsi, %rdx
    shr $32, %rdx     /* 値の上位32bit */
    wrmsr
    ret

.extern SyscallHandler /* C++側の関数 */

.global SyscallEntry
SyscallEntry:
    swapgs 
    mov %rsp, %gs:8
    mov %gs:0, %rsp

    /* レジスタ退避 */
    push %r11 /* RFLAGS */
    push %rcx /* RIP (ユーザーモードの戻り先) */
    push %rbp
    push %rbx
    push %r12
    push %r13
    push %r14
    push %r15
    push %rdi
    push %rsi
    push %rdx
    push %r10
    push %r8
    push %r9

    /* --- 引数のセットアップ --- */
    /* User  : RAX(No), RDI(1), RSI(2), RDX(3), R10(4) */
    /* Kernel: RDI(No), RSI(1), RDX(2), RCX(3), R8 (4), R9 (RIP用) */
    
    mov %rcx, %r9  /* 戻り先RIPを R9 に退避 (SyscallHandlerの第6引数として渡す) */
    mov %r10, %r8  /* User Arg4 (R10) -> Kernel Arg4 (R8) */
    mov %rdx, %rcx /* User Arg3 (RDX) -> Kernel Arg3 (RCX) */
    mov %rsi, %rdx /* User Arg2 (RSI) -> Kernel Arg2 (RDX) */
    mov %rdi, %rsi /* User Arg1 (RDI) -> Kernel Arg1 (RSI) */
    mov %rax, %rdi /* Syscall Number  -> Kernel Arg0 (RDI) */

    sti 
    call SyscallHandler
    cli 
    
    /* ここで RAX には SyscallHandler の戻り値が入っている */
    /* pop 命令は RAX を変更しないので、そのままユーザーに返る */

    /* レジスタ復帰 */
    pop %r9
    pop %r8
    pop %r10
    pop %rdx
    pop %rsi
    pop %rdi
    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %rbx
    pop %rbp
    pop %rcx /* RIP */
    pop %r11 /* RFLAGS */

    mov %gs:8, %rsp
    swapgs
    sysretq
