/**
 * @file  crt0.S
 * @brief 启动 ROM 的入口：关中断、等 SRAM 清零、建栈、清零 BSS
 *
 * 复位向量是本地地址 0（spec-mgmt-tile §8.1 第 1、2 步）。BSS 和栈都在
 * SRAM 末尾的 ROM 保留区，必须等 SRAM_STATUS.INIT_DONE 之后才能使用。
 *
 * @see spec-mgmt-tile
 */
    .option norvc
    .section .text.start
    .globl _start
_start:
    csrw    mie, zero
    csrci   mstatus, 8              /* MIE = 0 */
    la      t0, rom_trap
    csrw    mtvec, t0
    li      t0, 0x2000E000          /* SRAM_STATUS */
1:  lw      t1, 0(t0)
    andi    t1, t1, 1
    beqz    t1, 1b
    li      sp, 0x10200000          /* 栈顶：ROM 保留区末尾 */
    la      t0, __bss_start
    la      t1, __bss_end
2:  bgeu    t0, t1, 3f
    sd      zero, 0(t0)
    addi    t0, t0, 8
    j       2b
3:  call    rom_main
4:  wfi
    j       4b

/* 异常（例如链路故障时经 ML 的访问失败）：停住，等待 host 复位设备 */
    .balign 4
rom_trap:
5:  wfi
    j       5b
