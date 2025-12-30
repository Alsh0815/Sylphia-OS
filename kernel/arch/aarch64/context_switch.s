.text
.global SwitchContext

/* void SwitchContext(TaskContext* old_ctx, TaskContext* new_ctx); */
/* x0 = old_ctx, x1 = new_ctx */
/* TaskContext構造体は x86_64用とはメンバオフセットが異なるはずだが、
   ここではAArch64用に適当なオフセットで保存する。
   構造体の定義も合わせて変更する必要がある（C++側）。
   とりあえず 8バイトアライメントで詰めて保存する。
*/

SwitchContext:
    /* === save old_ctx (x0) === */
    /* x19-x29 (Callee-saved) */
    stp x19, x20, [x0, #0]
    stp x21, x22, [x0, #16]
    stp x23, x24, [x0, #32]
    stp x25, x26, [x0, #48]
    stp x27, x28, [x0, #64]
    str x29,      [x0, #80] /* FP */
    
    /* SP */
    mov x9, sp
    str x9,       [x0, #88]
    
    /* LR (Link Register - 戻りアドレス) */
    str x30,      [x0, #96]

    /* TTBR0_EL1 (Page Table) - 必要なら */
    mrs x9, ttbr0_el1
    str x9,       [x0, #104]

    /* === restore new_ctx (x1) === */
    
    /* TTBR0_EL1 */
    ldr x9,       [x1, #104]
    /* 現在の値と比較して異なれば更新 */
    mrs x10, ttbr0_el1
    cmp x9, x10
    beq .skip_ttbr
    msr ttbr0_el1, x9
    isb
    tlbi vmalle1 /* TLB flush (簡易的) */
    dsb ish
.skip_ttbr:

    /* SP */
    ldr x9,       [x1, #88]
    mov sp, x9

    /* LR */
    ldr x30,      [x1, #96]

    /* x19-x29, FP */
    ldp x19, x20, [x1, #0]
    ldp x21, x22, [x1, #16]
    ldp x23, x24, [x1, #32]
    ldp x25, x26, [x1, #48]
    ldp x27, x28, [x1, #64]
    ldr x29,      [x1, #80]

    ret
