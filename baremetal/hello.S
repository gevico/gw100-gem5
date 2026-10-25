/*
 * 最小 AArch64 裸机程序：向 VExpress_GEM5_Foundation 的 UART 写 G，
 * 然后通过 semihosting SYS_EXIT 结束仿真。
 */

.section .text
.global _start
_start:
    ldr     x0, =0x1C090000      /* PL011 data register */
    mov     w1, #0x47             /* 'G' */
    str     w1, [x0]

    mov     x0, #0x18             /* SYS_EXIT */
    mov     x1, #0                /* ADP_Stopped_ApplicationExit */
    hlt     #0xF000

1:  b       1b
