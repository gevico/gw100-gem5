/**
 * @file  vectors.S
 * @brief 异常向量表
 *
 * 测试不处理任何异常：16 个入口都把自己的编号交给 bm_exception，
 * 由它打印现场并以 CRASH 退出。中断测试以后需要时再替换对应入口。
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
    vector  5       /* 当前 EL，SPx：IRQ */
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
