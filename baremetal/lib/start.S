/**
 * @file  start.S
 * @brief 裸机测试的启动代码
 *
 * 设置栈和异常向量、清零 .bss，调用 test_main，再把返回值交给
 * bm_test_exit 结束仿真。只让 0 号核运行，其余核停在 wfe。
 * 不打开 MMU 和 cache。
 */

    .section .text.boot, "ax"
    .global _start
_start:
    mrs     x0, mpidr_el1
    and     x0, x0, #0xff
    cbnz    x0, park

    ldr     x0, =__stack_top
    mov     sp, x0

    /* 按当前异常级别设置向量表基址 */
    ldr     x0, =bm_vectors
    mrs     x1, CurrentEL
    lsr     x1, x1, #2
    cmp     x1, #3
    b.ne    1f
    msr     vbar_el3, x0
    b       3f
1:  cmp     x1, #2
    b.ne    2f
    msr     vbar_el2, x0
    b       3f
2:  msr     vbar_el1, x0
3:  isb

    /* 清零 .bss（起止都按 16 B 对齐，见 linker.ld） */
    ldr     x0, =__bss_start
    ldr     x1, =__bss_end
4:  cmp     x0, x1
    b.hs    5f
    str     xzr, [x0], #8
    b       4b
5:
    bl      test_main
    bl      bm_test_exit

park:
    wfe
    b       park
