.text

/* void SwitchContext(TaskContext* old_ctx, TaskContext* new_ctx); */
/* RDI = old_ctx, RSI = new_ctx */
.global SwitchContext
SwitchContext:
    /* === 現在のコンテキストを保存 (old_ctx) === */

    /* 汎用レジスタを保存（オフセット0-120） */
    mov %r15, 0(%rdi)
    mov %r14, 8(%rdi)
    mov %r13, 16(%rdi)
    mov %r12, 24(%rdi)
    mov %r11, 32(%rdi)
    mov %r10, 40(%rdi)
    mov %r9,  48(%rdi)
    mov %r8,  56(%rdi)

    mov %rdi, 64(%rdi)  /* 後で上書きされるが一応保存 */
    mov %rsi, 72(%rdi)
    mov %rbp, 80(%rdi)
    mov %rbx, 96(%rdi)
    mov %rdx, 104(%rdi)
    mov %rcx, 112(%rdi)
    mov %rax, 120(%rdi)

    /* RSPを保存（現在のスタックポインタ） */
    mov %rsp, 88(%rdi)

    /* RFLAGS を保存（オフセット184） */
    pushfq
    pop %rax
    mov %rax, 184(%rdi)

    /* CR3（ページテーブル）を保存（オフセット192） */
    mov %cr3, %rax
    mov %rax, 192(%rdi)

    /* === 新しいコンテキストを復元 (new_ctx) === */

    /* CR3（ページテーブル）を復元（オフセット192） */
    /* 異なるプロセスへの切り替え時にメモリ空間を変更 */
    mov 192(%rsi), %rax
    mov %cr3, %rcx
    cmp %rcx, %rax
    je .skip_cr3_load      /* 同じページテーブルなら切り替え不要 */
    mov %rax, %cr3           /* 新しいページテーブルをロード */
.skip_cr3_load:

    /* スタックポインタを復元（これが最重要） */
    /* 新規タスクの場合、スタックにはエントリーポイントが積まれている */
    mov 88(%rsi), %rsp

    /* RFLAGS を復元（オフセット184） */
    mov 184(%rsi), %rax
    push %rax
    popfq

    /* 汎用レジスタを復元（オフセット0-120） */
    mov 0(%rsi), %r15
    mov 8(%rsi), %r14
    mov 16(%rsi), %r13
    mov 24(%rsi), %r12
    mov 32(%rsi), %r11
    mov 40(%rsi), %r10
    mov 48(%rsi), %r9
    mov 56(%rsi), %r8

    mov 80(%rsi), %rbp
    mov 96(%rsi), %rbx
    mov 104(%rsi), %rdx
    mov 112(%rsi), %rcx
    mov 120(%rsi), %rax

    mov 64(%rsi), %rdi
    mov 72(%rsi), %rsi

    /* スタックに積まれたリターンアドレスへジャンプ */
    /* - 新規タスク: CreateTaskで積んだエントリーポイント */
    /* - 既存タスク: 前回のSwitchContext呼び出し元への戻りアドレス */
    ret
