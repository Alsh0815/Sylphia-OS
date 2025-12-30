.global _start
.extern main

.text
.align 4

_start:
    /* スタックアライメント (16バイト境界) */
    mov x29, #0   /* FPクリア */
    mov x30, #0   /* LRクリア */
    
    /* main(argc, argv) 呼び出し */
    /* カーネルからの引数は既に x0, x1 に入っている */
    bl main

    /* exit syscall */
    /* x0 には main の戻り値が入っている (status) */
    mov x8, #2    /* syscall number 2 (仮) - x86_64と合わせるなら要調整 */
    svc #0

.hang:
    wfe
    b .hang
