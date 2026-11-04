/**
 * @file  msix_intx.c
 * @brief MSI-X 与 INTx：消息、屏蔽与 Pending、Function Mask、BME、INTA
 *
 * 不加载固件，用 host 的 INT_SET 和 BAR0 的错误制造中断源。MSI-X 消息
 * 是设备发出的内存写，表项的地址指向 host 内存 BM_DMA_BUF_BASE 起的一块
 * 区域，测试从那里读回消息，检查设备一侧的语义（裸机库还没有配置
 * ITS，CPU 不接收这些消息）。INTx 从 GIC distributor 的 ISPENDR 观察
 * （spec-pcie §8，F8）。
 *
 * @see spec-pcie
 */

#include <stdint.h>

#include "bm/gw100.h"
#include "bm/mmio.h"
#include "bm/pci.h"
#include "bm/platform.h"
#include "bm/test.h"

/** 消息写到这里：向量 v 在 MSG + 16 × v */
#define MSG             (BM_DMA_BUF_BASE + 0x40000)
/** MSI-X 表 */
#define TABLE           0x2000
/** PBA */
#define PBA             0x3000
/** PCI INTx 的 SPI 基号（GenericArmPciHost int_base，按设备号取模 4） */
#define PCI_SPI_BASE    100
/** 等待消息的上限：100 us */
#define WAIT            10000ull

/** BAR0 */
static uintptr_t r;
/** 设备 */
static gw100_t g;
/** MSI-X capability 的偏移 */
static unsigned cap;

/** 向量 v 的消息字 */
static volatile uint32_t *
msg(unsigned v)
{
    return (volatile uint32_t *)(MSG + 16 * v);
}

/** 等待向量 v 的消息到达；超时返回 0 */
static int
wait_msg(unsigned v)
{
    uint64_t t0 = gw100_timestamp(&g);
    while (*msg(v) != 0xA000u + v) {
        if (gw100_timestamp(&g) - t0 > WAIT)
            return 0;
    }
    return 1;
}

/** 等一会儿，确认没有消息 */
static void
settle(void)
{
    uint64_t t0 = gw100_timestamp(&g);
    while (gw100_timestamp(&g) - t0 < WAIT)
        ;
}

/** 写 MSI-X 的 Message Control */
static void
msix_ctrl(int enable, int fmask)
{
    uint16_t mc = bm_pci_cfg_read16(g.bdf, cap + 2);
    mc = (uint16_t)((mc & 0x3FFF) | (fmask ? 0x4000 : 0) |
                    (enable ? 0x8000 : 0));
    bm_pci_cfg_write16(g.bdf, cap + 2, mc);
}

/** 设置 Command 寄存器中的 mask 位 */
static void
cmd_bits(uint16_t mask, int on)
{
    uint16_t c = bm_pci_cfg_read16(g.bdf, BM_PCI_COMMAND);
    c = (uint16_t)(on ? (c | mask) : (c & ~mask));
    bm_pci_cfg_write16(g.bdf, BM_PCI_COMMAND, c);
}

/** 表项 v 的 Mask */
static void
vec_mask(unsigned v, int m)
{
    bm_write32(r + TABLE + 16 * v + 12, m ? 1 : 0);
}

/** INTA 对应的 SPI 是否 pending */
static int
inta_pending(void)
{
    unsigned spi = PCI_SPI_BASE + g.bdf.dev % 4;
    uint32_t v = bm_read32(BM_GICD_BASE + 0x200 + 4 * (spi / 32));
    return (v >> (spi % 32)) & 1;
}

/** 测试入口：每个 test 实现它，返回 0 表示通过，非 0 表示失败 */
int
test_main(void)
{
    g = gw100_open();
    r = g.bar0;

    TEST_INFO("MSI-X capability\n");
    cap = bm_pci_cfg_read8(g.bdf, 0x34);
    while (cap && bm_pci_cfg_read8(g.bdf, cap) != 0x11)
        cap = bm_pci_cfg_read8(g.bdf, cap + 1);
    TEST_ASSERT(cap != 0);
    TEST_EQ(bm_pci_cfg_read16(g.bdf, cap + 2) & 0x7FF, 31);
    TEST_EQ(bm_pci_cfg_read32(g.bdf, cap + 4), TABLE);   /* BIR 0 */
    TEST_EQ(bm_pci_cfg_read32(g.bdf, cap + 8), PBA);
    TEST_EQ(bm_read32(r + TABLE + 12), 1);               /* Mask 复位为 1 */

    for (unsigned v = 0; v < 2; v++) {
        bm_write32(r + TABLE + 16 * v, (uint32_t)(MSG + 16 * v));
        bm_write32(r + TABLE + 16 * v + 4, 0);
        bm_write32(r + TABLE + 16 * v + 8, 0xA000u + v);
        vec_mask(v, 0);
        *msg(v) = 0;
    }
    cmd_bits(BM_PCI_CMD_MASTER, 1);
    msix_ctrl(1, 0);

    TEST_INFO("INT_SET 发出消息\n");
    bm_write32(r + GW100_INT_SET, GW100_INT_ADMIN);
    TEST_ASSERT(wait_msg(0));
    TEST_ASSERT(bm_read32(r + GW100_INT_STATUS) & GW100_INT_ADMIN);
    bm_write32(r + GW100_INT_STATUS, 0xFFFFFFFFu);

    TEST_INFO("屏蔽时置 Pending，解除后发出\n");
    *msg(0) = 0;
    vec_mask(0, 1);
    bm_write32(r + GW100_INT_SET, GW100_INT_FW_EVENT);
    settle();
    TEST_EQ(*msg(0), 0);
    TEST_EQ(bm_read32(r + PBA) & 1, 1);
    vec_mask(0, 0);
    TEST_ASSERT(wait_msg(0));
    TEST_EQ(bm_read32(r + PBA) & 1, 0);

    TEST_INFO("屏蔽期间事件被处理：撤销 Pending\n");
    *msg(0) = 0;
    vec_mask(0, 1);
    bm_write32(r + GW100_INT_SET, GW100_INT_ADMIN);
    TEST_EQ(bm_read32(r + PBA) & 1, 1);
    bm_write32(r + GW100_INT_STATUS, 0xFFFFFFFFu);
    TEST_EQ(bm_read32(r + PBA) & 1, 0);
    vec_mask(0, 0);
    settle();
    TEST_EQ(*msg(0), 0);

    TEST_INFO("错误经向量 1 通知\n");
    bm_write32(r + 0x040, 0);                /* 保留偏移 */
    TEST_ASSERT(wait_msg(1));
    TEST_ASSERT(bm_read32(r + GW100_INT_STATUS) & GW100_INT_ERROR);
    bm_write32(r + GW100_ERR_STATUS, 0xFFFFFFFFu);
    bm_write32(r + GW100_INT_STATUS, 0xFFFFFFFFu);

    TEST_INFO("Function Mask 与 Bus Master Enable\n");
    *msg(0) = 0;
    msix_ctrl(1, 1);
    bm_write32(r + GW100_INT_SET, GW100_INT_ADMIN);
    settle();
    TEST_EQ(*msg(0), 0);
    TEST_EQ(bm_read32(r + PBA) & 1, 1);
    msix_ctrl(1, 0);
    TEST_ASSERT(wait_msg(0));
    *msg(0) = 0;
    cmd_bits(BM_PCI_CMD_MASTER, 0);
    bm_write32(r + GW100_INT_SET, GW100_INT_ADMIN);
    settle();
    TEST_EQ(*msg(0), 0);
    TEST_EQ(bm_read32(r + PBA) & 1, 1);
    cmd_bits(BM_PCI_CMD_MASTER, 1);
    TEST_ASSERT(wait_msg(0));
    bm_write32(r + GW100_INT_STATUS, 0xFFFFFFFFu);

    TEST_INFO("INTx\n");
    msix_ctrl(0, 0);
    TEST_EQ(inta_pending(), 0);
    bm_write32(r + GW100_INT_MASK, ~GW100_INT_ADMIN);
    bm_write32(r + GW100_INT_SET, GW100_INT_ADMIN);
    TEST_EQ(inta_pending(), 1);
    bm_write32(r + GW100_INT_STATUS, GW100_INT_ADMIN);
    TEST_EQ(inta_pending(), 0);
    cmd_bits(BM_PCI_CMD_INTX_DISABLE, 1);
    bm_write32(r + GW100_INT_SET, GW100_INT_ADMIN);
    TEST_EQ(inta_pending(), 0);
    cmd_bits(BM_PCI_CMD_INTX_DISABLE, 0);
    TEST_EQ(inta_pending(), 1);
    bm_write32(r + GW100_INT_STATUS, GW100_INT_ADMIN);
    TEST_EQ(inta_pending(), 0);
    return 0;
}
