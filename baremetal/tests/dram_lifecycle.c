/**
 * @file  dram_lifecycle.c
 * @brief FLR 与有保持时间的 D3hot/D0 内存生命周期
 *
 * @see spec-dram, spec-clock-reset-power
 */
#include "bm/gw100.h"
#include "bm/hdi.h"
#include "bm/mmio.h"
#include "bm/platform.h"
#include "bm/test.h"

/** PM Control/Status 配置偏移 */
#define PMCSR 0x44
/** Device Control 配置偏移 */
#define DEVCTL 0x58
/** 管理队列使用的两个 host 页 */
#define ADMIN BM_DMA_BUF_BASE
/** 上下文队列所在的 host 页 */
#define CONTEXT_SQ (BM_DMA_BUF_BASE + 0x2000)
/** 上下文完成队列所在的 host 页 */
#define CONTEXT_CQ (BM_DMA_BUF_BASE + 0x3000)
/** 被复位打断的 host 数据区 */
#define HOST_DATA (BM_DMA_BUF_BASE + 0x100000)
/** 被复位打断的设备数据区，与保持见证值分开 */
#define DEVICE_DATA 0x800000
/** 一个完整 COPY 的长度，字节 */
#define COPY_BYTES 0x100000
/** BAR0 中通道 1 的只读镜像 */
#define CHANNEL1 (0x6000 + 0x100 + 0x40)

/** 发起 COPY 并确认已有实际数据传输，留下其余操作在途 */
static void
start_copy(gw100_t *g, unsigned opcode)
{
    hdi_t h;
    hdi_queue_t q;
    hdi_cqe_t e;
    uint32_t s[16];
    uint16_t command = bm_pci_cfg_read16(g->bdf, BM_PCI_COMMAND);
    bm_pci_cfg_write16(g->bdf, BM_PCI_COMMAND, command | BM_PCI_CMD_MASTER);
    TEST_EQ(hdi_enable(&h, g, ADMIN, 8, 8), 0);
    hdi_sqe(s, HDI_OP_CREATE_CTX);
    s[10] = (2u << 8) | (1u << 24);
    s[11] = 7u | (7u << 16);
    s[12] = CONTEXT_SQ;
    s[14] = CONTEXT_CQ;
    TEST_EQ(hdi_cmd(&h, &h.admin, s, &e), HDI_SC_SUCCESS);
    hdi_queue_init(&h, &q, CONTEXT_SQ, 8, CONTEXT_CQ, 8, 1);
    hdi_sqe(s, HDI_OP_SET_REGION);
    s[10] = (HDI_PERM_R | HDI_PERM_W) << 16;
    s[12] = DEVICE_DATA;
    s[14] = COPY_BYTES;
    TEST_EQ(hdi_cmd(&h, &h.admin, s, &e), HDI_SC_SUCCESS);
    hdi_sqe(s, opcode);
    hdi_dptr(s, HOST_DATA, COPY_BYTES, HDI_SGL_DATA);
    s[12] = DEVICE_DATA;
    s[14] = COPY_BYTES;
    hdi_submit(&h, &q, s);
    uint64_t start = gw100_timestamp(g);
    while (!bm_read32(g->bar0 + CHANNEL1 + 0x30)) {
        TEST_ASSERT(gw100_timestamp(g) - start < 1000000ull);
        bm_relax();
    }
    TEST_EQ(bm_read32(g->bar0 + CHANNEL1 + 4) & 3, 1);
}

/** 使用设备时间戳等待指定数量的 10 ns 周期 */
static void
wait_ticks(const gw100_t *g, uint64_t ticks)
{
    uint64_t start = gw100_timestamp(g);
    while (gw100_timestamp(g) - start < ticks) {
        bm_relax();
    }
}

/** 在重新开放的数据窗口检查八个通道的内容 */
static void
check_data(gw100_t *g)
{
    gw100_win_set(g, 0, 0, GW100_WIN_EN);
    for (unsigned ch = 0; ch < 8; ++ch) {
        TEST_EQ(bm_read64(g->bar2 + 0x10000 + ch * 256),
                0xCAFE123400000000ull | ch);
    }
}

/** 测试入口：每个 test 实现它，返回 0 表示通过，非 0 表示失败 */
int
test_main(void)
{
    gw100_t g = gw100_open();
    gw100_boot(&g);
    gw100_win_set(&g, 0, 0, GW100_WIN_EN);
    for (unsigned ch = 0; ch < 8; ++ch) {
        bm_write64(g.bar2 + 0x10000 + ch * 256, 0xCAFE123400000000ull | ch);
    }
    for (unsigned cycle = 0; cycle < 3; ++cycle) {
        start_copy(&g, HDI_OP_COPY_H2D);
        uint16_t control = bm_pci_cfg_read16(g.bdf, DEVCTL);
        bm_pci_cfg_write16(g.bdf, DEVCTL, control | 0x8000);
        g = gw100_open();
        gw100_boot(&g);
        check_data(&g);
        start_copy(&g, HDI_OP_COPY_D2H);
        bm_pci_cfg_write16(g.bdf, PMCSR, 3);
        TEST_EQ(bm_read32(g.bar0 + GW100_BOOT_STATUS) & GW100_BOOT_FW_READY,
                0);
        wait_ticks(&g, 100000);
        uint64_t start = gw100_timestamp(&g);
        bm_pci_cfg_write16(g.bdf, PMCSR, 0);
        g = gw100_open();
        gw100_boot(&g);
        TEST_ASSERT(gw100_timestamp(&g) - start < 10000000ull);
        check_data(&g);
    }
    TEST_EQ(bm_read32(g.bar0 + GW100_DEV_STATUS) & GW100_DEV_STATUS_FATAL, 0);
    TEST_INFO("三轮 FLR 与自刷新恢复通过\n");
    return 0;
}
