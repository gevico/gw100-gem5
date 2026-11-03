/**
 * @file  crt0.S
 * @brief 固件入口：建栈、清零 BSS，以启动信息调用 fw_main
 *
 * 启动 ROM 跳转到这里时 a0 为 hart ID，a1 为启动信息的地址
 * （spec-mgmt-tile §8.3）。栈在运行区顶端、ROM 保留区之下。
 *
 * @see spec-mgmt-tile
 */
    .option norvc
    .section .text.entry
    .globl _start
_start:
    li      sp, 0x101FF000          /* ROM 保留区之下 */
    la      t0, __bss_start
    la      t1, __bss_end
1:  bgeu    t0, t1, 2f
    sd      zero, 0(t0)
    addi    t0, t0, 8
    j       1b
2:  call    fw_main
3:  wfi
    j       3b
