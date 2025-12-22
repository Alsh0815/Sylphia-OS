bits 64
section .text

; void SwitchContext(TaskContext* old_ctx, TaskContext* new_ctx);
; RDI = old_ctx, RSI = new_ctx
global SwitchContext
SwitchContext:
    ; === 現在のコンテキストを保存 (old_ctx) ===

    ; 汎用レジスタを保存（オフセット0-120）
    mov [rdi + 0],  r15
    mov [rdi + 8],  r14
    mov [rdi + 16], r13
    mov [rdi + 24], r12
    mov [rdi + 32], r11
    mov [rdi + 40], r10
    mov [rdi + 48], r9
    mov [rdi + 56], r8

    mov [rdi + 64], rdi  ; 後で上書きされるが一応保存
    mov [rdi + 72], rsi
    mov [rdi + 80], rbp
    mov [rdi + 96], rbx
    mov [rdi + 104], rdx
    mov [rdi + 112], rcx
    mov [rdi + 120], rax

    ; RSPを保存（現在のスタックポインタ）
    mov [rdi + 88], rsp

    ; RFLAGS を保存（オフセット184）
    pushfq
    pop rax
    mov [rdi + 184], rax

    ; CR3（ページテーブル）を保存（オフセット192）
    mov rax, cr3
    mov [rdi + 192], rax

    ; === 新しいコンテキストを復元 (new_ctx) ===

    ; CR3（ページテーブル）を復元（オフセット192）
    ; 異なるプロセスへの切り替え時にメモリ空間を変更
    mov rax, [rsi + 192]
    mov rcx, cr3
    cmp rax, rcx
    je .skip_cr3_load      ; 同じページテーブルなら切り替え不要
    mov cr3, rax           ; 新しいページテーブルをロード
.skip_cr3_load:

    ; スタックポインタを復元（これが最重要）
    ; 新規タスクの場合、スタックにはエントリーポイントが積まれている
    mov rsp, [rsi + 88]

    ; RFLAGS を復元（オフセット184）
    mov rax, [rsi + 184]
    push rax
    popfq

    ; 汎用レジスタを復元（オフセット0-120）
    mov r15, [rsi + 0]
    mov r14, [rsi + 8]
    mov r13, [rsi + 16]
    mov r12, [rsi + 24]
    mov r11, [rsi + 32]
    mov r10, [rsi + 40]
    mov r9,  [rsi + 48]
    mov r8,  [rsi + 56]

    mov rbp, [rsi + 80]
    mov rbx, [rsi + 96]
    mov rdx, [rsi + 104]
    mov rcx, [rsi + 112]
    mov rax, [rsi + 120]

    mov rdi, [rsi + 64]
    mov rsi, [rsi + 72]

    ; スタックに積まれたリターンアドレスへジャンプ
    ; - 新規タスク: CreateTaskで積んだエントリーポイント
    ; - 既存タスク: 前回のSwitchContext呼び出し元への戻りアドレス
    ret

