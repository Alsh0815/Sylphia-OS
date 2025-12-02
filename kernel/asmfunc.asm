bits 64
section .bss

global g_kernel_rsp_save
g_kernel_rsp_save: resq 1

section .text

; void EnableSSE();
global EnableSSE
EnableSSE:
    ; CR0 読み込み
    mov rax, cr0
    ; EM (Emulation) ビット (bit 2) をクリア (0)
    and ax, 0xFFFB
    ; MP (Monitor Coprocessor) ビット (bit 1) をセット (1)
    or ax, 0x0002
    mov cr0, rax
    ; CR4 読み込み
    mov rax, cr4
    ; OSFXSR (OS Support for FXSAVE/FXRSTOR) ビット (bit 9) をセット (1)
    or ax, 3 << 9  ; bit 9 と bit 10 (OSXMMEXCPT) をまとめてセット
    mov cr4, rax
    ret

global EnterUserMode

; void EnterUserMode(uint64_t entry_point, uint64_t user_stack_top, int argc, uint64_t argv_ptr);
EnterUserMode:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    mov [g_kernel_rsp_save], rsp
    ; 引数: RDI = entry_point, RSI = user_stack_tops
    cli                 ; 割り込み禁止（コンテキストスイッチ中）
    ; 1. SS (User Data Segment)
    ; syscall.cpp の STAR設定 (0x18<<48) より、
    ; Sysret CS = 0x18+16 = 0x28, Sysret SS = 0x18+8 = 0x20 と想定されます。
    ; これに RPL=3 を付与 -> 0x23
    mov rax, 0x23
    push rax
    ; 2. RSP (User Stack Pointer)
    push rsi
    ; 3. RFLAGS
    ; IF(Interrupt Enable)=1 (0x200) をセットして、ユーザーモードで割り込み許可
    pushf
    pop rax
    or rax, 0x200 
    push rax
    ; 4. CS (User Code Segment)
    ; Sysret CS = 0x28, RPL=3 -> 0x2B
    mov rax, 0x2B
    push rax
    ; 5. RIP (Entry Point)
    push rdi

    ; Setup Arguments for User Mode
    ; Kernel (SysV): RDX=argc, RCX=argv
    ; User (SysV):   RDI=argc, RSI=argv
    
    mov rdi, rdx ; argc
    mov rsi, rcx ; argv

    mov r12, rdx ; Preserve argc in R12 just in case
    
    ; セグメントレジスタの初期化 (DS, ES, FS, GS)
    mov ax, 0x23  ; User Data Segment
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    iretq

global ExitApp
ExitApp:
    mov rsp, [g_kernel_rsp_save]
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    cli
    swapgs
    ret

; void LoadCR3(uint64_t pml4_addr);
global LoadCR3
LoadCR3:
    mov cr3, rdi
    ret

; void LoadGDT(uint16_t limit, uint64_t offset);
global LoadGDT
LoadGDT:
    push rbp
    mov rbp, rsp
    sub rsp, 16         ; スタック上にGDT記述子用の領域を確保 (10バイト必要だがアライメントのため16)

    mov [rsp], di       ; Limit (16bit) をセット
    mov [rsp+2], rsi    ; Base (64bit) をセット
    lgdt [rsp]          ; GDTをロード

    ; GDTをロードしただけではCSレジスタのキャッシュが古いままなので、
    ; Far Return (retfq) を使って強制的に CS を 0x08 (Kernel Code) にリロードする
    
    push 0x08           ; 新しいCS (Kernel Code Segment)
    lea rax, [.reload_cs]
    push rax            ; 戻り先アドレス (RIP)
    retfq               ; [CS] [RIP] をポップしてジャンプ

.reload_cs:
    mov rsp, rbp
    pop rbp
    ret

; void LoadTR(uint16_t sel);
global LoadTR
LoadTR:
    ltr di
    ret

; uint64_t GetCR2();
global GetCR2
GetCR2:
    mov rax, cr2
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

; void SetDSAll(uint16_t value);
global SetDSAll
SetDSAll:
    mov ds, di
    mov es, di
    mov fs, di
    mov gs, di
    mov ss, di          ; SSも一緒に設定
    ret



; uint64_t ReadMSR(uint32_t msr);
global ReadMSR
ReadMSR:
    ; 引数: RDI = msr番号
    ; rdmsr命令: ECXにMSR番号を入れて実行 -> EDX:EAXに値が返る
    mov rcx, rdi
    rdmsr
    ; EDX:EAX を 64bitの RAX にまとめる (戻り値)
    shl rdx, 32
    or rax, rdx
    ret

; void WriteMSR(uint32_t msr, uint64_t value);
global WriteMSR
WriteMSR:
    ; 引数: RDI = msr番号, RSI = 書き込む値 (64bit)
    ; wrmsr命令: ECXにMSR番号, EDX:EAXに値(64bit)を入れて実行する
    mov rcx, rdi    ; MSR番号
    mov eax, esi    ; 値の下位32bit
    mov rdx, rsi
    shr rdx, 32     ; 値の上位32bit
    wrmsr
    ret

extern SyscallHandler ; C++側の関数

global SyscallEntry
SyscallEntry:
    swapgs 
    mov [gs:8], rsp
    mov rsp, [gs:0]

    ; レジスタ退避
    push r11 ; RFLAGS
    push rcx ; RIP (ユーザーモードの戻り先)
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15

    ; --- 引数のセットアップ ---
    ; User  : RAX(No), RDI(1), RSI(2), RDX(3), R10(4)
    ; Kernel: RDI(No), RSI(1), RDX(2), RCX(3), R8 (4), R9 (RIP用)
    
    mov r9, rcx  ; 戻り先RIPを R9 に退避 (SyscallHandlerの第6引数として渡す)
    mov r8, r10  ; User Arg4 (R10) -> Kernel Arg4 (R8)
    mov rcx, rdx ; User Arg3 (RDX) -> Kernel Arg3 (RCX)
    mov rdx, rsi ; User Arg2 (RSI) -> Kernel Arg2 (RDX)
    mov rsi, rdi ; User Arg1 (RDI) -> Kernel Arg1 (RSI)
    mov rdi, rax ; Syscall Number  -> Kernel Arg0 (RDI)

    sti 
    call SyscallHandler
    cli 
    
    ; ここで RAX には SyscallHandler の戻り値が入っている
    ; pop 命令は RAX を変更しないので、そのままユーザーに返る

    ; レジスタ復帰
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    pop rcx ; RIP
    pop r11 ; RFLAGS

    mov rsp, [gs:8]
    swapgs
    db 0x48, 0x0f, 0x07 ; sysretq