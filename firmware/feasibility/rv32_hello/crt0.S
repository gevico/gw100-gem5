/**
 * @file  crt0.S
 * @brief 多 ISA 可行性测试的 RV32 程序入口、异常入口与 semihosting 调用
 *
 * 可行性测试，不是产品程序。复位向量是本地地址 0；关中断、设异常向量、
 * 建栈、清零 BSS 后调用 main，返回后在 wfi 中停住。异常交给 on_trap
 * 报告并以失败退出。
 *
 * @see gem5-ext/configs/feasibility/README.md
 */
    .option norvc
    .section .text.start
    .globl _start
_start:
    csrw    mie, zero
    csrci   mstatus, 8              /* MIE = 0 */
    la      t0, trap_entry
    csrw    mtvec, t0
    la      sp, __stack_top
    la      t0, __bss_start
    la      t1, __bss_end
1:  bgeu    t0, t1, 2f
    sw      zero, 0(t0)
    addi    t0, t0, 4
    j       1b
2:  call    main
3:  wfi
    j       3b

/* 异常：报告 mcause、mepc 后以失败退出，然后停住 */
    .balign 4
trap_entry:
    csrr    a0, mcause
    csrr    a1, mepc
    call    on_trap
4:  wfi
    j       4b

/*
 * uint32_t semihost(uint32_t op, const void *arg)
 * RISC-V semihosting 的调用序列：三条不压缩的指令必须在同一页内，
 * 按 16 字节对齐保证这一点
 */
    .balign 16
    .globl semihost
semihost:
    slli    zero, zero, 0x1f
    ebreak
    srai    zero, zero, 7
    ret
