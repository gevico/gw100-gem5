/**
 * @file  sram_test.c
 * @brief 管理 CPU 逐 bank 与部分写自检
 * @see spec-sram
 */
#include <stdint.h>
#include "gw100_mgmt.h"
#include "gw100_image.h"
#include "gw100_flr.h"
#include "gw100_hdma.h"

/** 固件运行区结束位置，用于检查测试临时页。 */
extern uint8_t __bss_end[];

/** 验证真实 CPU 访问错误及 UE 寄存器后发布测试结果。 */
__attribute__((used, noreturn)) void
sram_fault(uint64_t cause, uint64_t address)
{
    uintptr_t expected = GW_SRAM_BASE + GW_SRAM_SIZE /
                         GW_SRAM_MANAGEMENT_BANKS - 8192;
    uint64_t recorded = *(volatile uint64_t *)(GW_SRAM_STATUS + 0x10);
    uint32_t result = (cause == 1 || cause == 5 || cause == 7) &&
        address == expected + (cause == 1 ? 2 : 0) &&
        recorded == expected && (gw_read32(GW_SRAM_STATUS + 4) & 2) &&
        (gw_read32(0x2C001000UL) & (1u << 5)) ?
        (0x5A4DF000u | (uint32_t)cause) : 0x5A4DBAD0u;
    gw_write32(GW_FW_BOOT_STATUS,
               GW_BOOT_STATUS(GW_STAGE_FAILED, 0, GW_ERR_INIT));
    gw_write32(GW_FW_VERSION_FW, result);
    gw_halt();
    __builtin_unreachable();
}

/** 将机器异常原因和地址交给终止式验证程序。 */
__attribute__((naked)) static void
sram_trap(void)
{
    __asm__ volatile("csrr a0, mcause\ncsrr a1, mtval\ntail sram_fault");
}

/** 使用非法 TYPE 触发描述符读取和 DW7 回写，保持数据通路空闲。 */
static int
ml_selftest(struct gw_hdma_chan *channel)
{
    struct gw_hdma_desc descriptor = {0};
    descriptor.dw6 = 0x11223344;
    gw_hdma_put(channel, &descriptor);
    gw_hdma_commit(channel);
    uint64_t start = gw_read64(GW_CLINT_MTIME);
    while ((gw_read32(GW_HDMA_CH(channel->ch, GW_CH_STATUS)) & 3) !=
           GW_CH_ERRSTOP) {
        if (gw_read64(GW_CLINT_MTIME) - start > 100000)
            return 10;
    }
    uint32_t error = gw_read32(GW_HDMA_CH(channel->ch, GW_CH_ERR));
    unsigned phase = (error >> 8) & 3;
    int result = 0;
    if (phase == 1 || phase == 2) {
        uintptr_t at = (uintptr_t)channel->ring + (phase == 1 ? 0 : 24);
        if ((error & 0xFF) != 5 ||
            gw_read32(GW_HDMA_CH(channel->ch, GW_CH_HEAD)) != 0 ||
            !(gw_read32(GW_SRAM_STATUS + 4) & 2) ||
            gw_read64(GW_SRAM_STATUS + 0x10) != at ||
            !(gw_read32(GW_HIF_BASE + 0x13000) & (1u << 4)) ||
            !(gw_read32(0x2000D004UL) & (1u << 8)))
            return 11;
        if (phase == 1 && channel->ring[0].dw7 != 0)
            return 12;
        /* 完整码字重写修复 UE，避免 CPU 再访问故障码字。 */
        *(volatile uint64_t *)at = phase == 1 ? 0 : descriptor.dw6;
        result = phase == 1 ? 8 : 9;
    } else if ((error & 0xFF) != 7 || channel->ring[0].dw7 != 0x701) {
        return 13;
    }
    gw_write32(GW_SRAM_STATUS + 4, 2);
    gw_write32(GW_HIF_BASE + 0x13000, 1u << 4);
    gw_write32(GW_EVT_PENDING, GW_EVT_HDMA_ERR);
    gw_hdma_chan_init(channel, channel->ch, channel->ring, channel->entries);
    return result;
}

/** 数字阵列、CPU 异常与 ML 的真实访问路径。 */
int
sram_selftest(struct gw_hdma_chan *channel)
{
    uintptr_t previous;
    __asm__ volatile("csrrw %0, mtvec, %1" : "=r"(previous) :
                     "r"(sram_trap) : "memory");
    if (!(gw_read32(GW_SRAM_STATUS) & 1))
        return 1;
    gw_write32(GW_SRAM_STATUS + 0x0C, 1);
    /* 测试镜像的末尾与 BSS 后保留空闲页，临时保存并恢复原内容。 */
    for (unsigned bank = 0; bank < GW_SRAM_MANAGEMENT_BANKS; ++bank) {
        uintptr_t offset = (bank + 1) *
            (GW_SRAM_SIZE / GW_SRAM_MANAGEMENT_BANKS) - 8192;
        const struct gw_image_hdr *image = (const void *)GW_SRAM_BASE;
        if (offset < image->image_size ||
            (GW_SRAM_BASE + offset >= GW_RUN_BASE &&
             GW_SRAM_BASE + offset < (uintptr_t)__bss_end))
            return 5;
        volatile uint64_t *at = (volatile uint64_t *)(GW_SRAM_BASE + offset);
        uint64_t saved[8];
        for (unsigned i = 0; i < 8; ++i) {
            saved[i] = at[i];
            at[i] = 0x0123456789ABCDEFULL ^ ((uint64_t)bank << 32) ^ i;
        }
        __asm__ volatile("fence rw,rw" ::: "memory");
        for (unsigned i = 0; i < 8; ++i)
            if (at[i] != (0x0123456789ABCDEFULL ^ ((uint64_t)bank << 32) ^ i))
                return 2;
        *(volatile uint8_t *)at = 0x5A;
        *(volatile uint16_t *)((uintptr_t)at + 2) = 0x1357;
        *(volatile uint32_t *)((uintptr_t)at + 4) = 0x2468ACE0;
        __asm__ volatile("fence rw,rw" ::: "memory");
        if (at[0] != 0x2468ACE01357CD5AULL)
            return 3;
        for (unsigned i = 0; i < 8; ++i)
            at[i] = saved[i];
    }
    uintptr_t code = GW_SRAM_BASE + GW_SRAM_SIZE /
                     GW_SRAM_MANAGEMENT_BANKS - 8192;
    uint64_t saved = *(volatile uint64_t *)code;
    /* c.nop / c.li a0,0 / c.jr ra / c.nop，从字内 +2 B 取指。 */
    *(volatile uint64_t *)code = 0x0001808245010001ULL;
    __asm__ volatile("fence.i" ::: "memory");
    if (((int (*)(void))(code + 2))())
        return 7;
    *(volatile uint64_t *)code = saved;
    uint32_t status = gw_read32(GW_SRAM_STATUS + 4);
    uint32_t pending = gw_read32(0x2C001000UL);
    gw_write32(GW_SRAM_STATUS + 0x0C, 0);
    __asm__ volatile("csrw mtvec, %0" :: "r"(previous) : "memory");
    if (status == 1 && gw_read32(GW_SRAM_STATUS + 8) && (pending & (1u << 4)))
        return 6;
    return status ? 4 : ml_selftest(channel);
}
