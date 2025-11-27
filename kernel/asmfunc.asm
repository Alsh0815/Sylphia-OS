bits 64
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
    ; --- ユーザーモードから遷移直後 ---
    ; RCX = 戻り先RIP (syscall命令の次の命令)
    ; R11 = 保存されたRFLAGS
    ; RSP = ユーザースタックポインタ (まだ切り替わっていない)
    ; GS  = User GS (swapgs前)
    ; 1. GSをカーネル用に切り替え
    swapgs ; これで gs:[0] が g_syscall_context を指すようになる
    ; 2. スタック切り替え
    ; ユーザースタックを保存し、カーネルスタックをロード
    mov [gs:8], rsp     ; g_syscall_context->user_stack_ptr に RSP を保存
    mov rsp, [gs:0]     ; g_syscall_context->kernel_stack_ptr を RSP にロード
    ; 3. レジスタ退避 (カーネルスタック上)
    push r11 ; RFLAGS
    push rcx ; RIP
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    ; 4. C++関数の呼び出し準備
    ; ユーザー側(syscall ABI): RAX(No), RDI(Arg1), RSI(Arg2), RDX(Arg3), R10(Arg4)...
    ; カーネル側(System V ABI): RDI, RSI, RDX, RCX, R8, R9...
    ; マッピング:
    ; SyscallHandler(num, arg1, arg2, arg3)
    ; RDI <= RAX (Syscall Number)
    ; RSI <= RDI (Arg1)
    ; RDX <= RSI (Arg2)
    ; RCX <= RDX (Arg3)
    ; ※ RCXはRIP退避用に使われているので上書き注意
    ; ※ R10はユーザー側の第4引数だが、syscall命令でRCXが破壊されるため
    ;    ユーザー側は第4引数をR10に入れて渡す規約が一般的 (Linuxなど)
    mov rcx, rdx ; Arg3
    mov rdx, rsi ; Arg2
    mov rsi, rdi ; Arg1
    mov rdi, rax ; Syscall Number (ユーザーはRAXに入れてくる)
    sti ; 必要であれば割り込み許可 (今回はハンドラ内で重い処理をしないならCLIのままでも可)
    call SyscallHandler
    cli ; 復帰処理中は割り込み禁止
    ; 5. レジスタ復帰
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    pop rcx ; RIP (ユーザー空間のアドレス)
    pop r11 ; RFLAGS
    ; 6. スタックをユーザー用に戻す
    mov rsp, [gs:8] ; user_stack_ptr を戻す
    ; 7. GSをユーザー用に戻す
    swapgs
    ; 8. ユーザーモードへ復帰
    sysretq