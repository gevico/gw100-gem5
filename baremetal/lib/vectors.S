/**
 * @file  vectors.S
 * @brief 异常向量表
 *
 * 当前 EL、SPx 的 IRQ 入口（程序在 EL3、使用 SP_EL3 运行时 IRQ 进入
 * 这里）保存调用者保存的寄存器，调用 bm_irq_dispatch（lib/gic.c），恢复
 * 后 eret；其余 15 个入口都把自己的编号交给 bm_exception，由它打印现场
 * 并以 CRASH 退出。处理期间 PSTATE.I 保持置位，不嵌套，ELR、SPSR 不会
 * 被覆盖。
 */

    .macro  vector idx
    .balign 0x80
    mov     x0, #\idx
    b       bm_exception_entry
    .endm

    .section .text.vectors, "ax"
    .balign 0x800
    .global bm_vectors
bm_vectors:
    vector  0       /* 当前 EL，SP0：同步 */
    vector  1       /* 当前 EL，SP0：IRQ */
    vector  2       /* 当前 EL，SP0：FIQ */
    vector  3       /* 当前 EL，SP0：SError */
    vector  4       /* 当前 EL，SPx：同步 */
    .balign 0x80
    b       bm_irq_entry    /* 当前 EL，SPx：IRQ */
    vector  6       /* 当前 EL，SPx：FIQ */
    vector  7       /* 当前 EL，SPx：SError */
    vector  8       /* 低 EL，AArch64：同步 */
    vector  9       /* 低 EL，AArch64：IRQ */
    vector  10      /* 低 EL，AArch64：FIQ */
    vector  11      /* 低 EL，AArch64：SError */
    vector  12      /* 低 EL，AArch32：同步 */
    vector  13      /* 低 EL，AArch32：IRQ */
    vector  14      /* 低 EL，AArch32：FIQ */
    vector  15      /* 低 EL，AArch32：SError */

    .text
bm_exception_entry:
    b       bm_exception

/* IRQ：保存 x0 – x18、x29、x30（C 代码可能改写它们），处理后返回 */
bm_irq_entry:
    sub     sp, sp, #176
    stp     x0, x1, [sp, #0]
    stp     x2, x3, [sp, #16]
    stp     x4, x5, [sp, #32]
    stp     x6, x7, [sp, #48]
    stp     x8, x9, [sp, #64]
    stp     x10, x11, [sp, #80]
    stp     x12, x13, [sp, #96]
    stp     x14, x15, [sp, #112]
    stp     x16, x17, [sp, #128]
    stp     x18, x29, [sp, #144]
    str     x30, [sp, #160]
    bl      bm_irq_dispatch
    ldp     x0, x1, [sp, #0]
    ldp     x2, x3, [sp, #16]
    ldp     x4, x5, [sp, #32]
    ldp     x6, x7, [sp, #48]
    ldp     x8, x9, [sp, #64]
    ldp     x10, x11, [sp, #80]
    ldp     x12, x13, [sp, #96]
    ldp     x14, x15, [sp, #112]
    ldp     x16, x17, [sp, #128]
    ldp     x18, x29, [sp, #144]
    ldr     x30, [sp, #160]
    add     sp, sp, #176
    eret
