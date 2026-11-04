/**
 * @file  hdma_test.c
 * @brief HDMA 的测试固件：按约定的 host 地址做几次传输并报告结果
 *
 * 只编进 fw_hdma.img（GW_FW_HDMA_TEST），供裸机测试 hdma_basic 使用。
 * 命令协议（HDI）实现之前，固件无从得知 host 的缓冲地址，测试两边约定
 * host 内存中 HOST_BUF 起的一块区域（与 baremetal 的 BM_DMA_BUF_BASE
 * 相同），host 在加载固件之前准备好数据和 SGL：
 * - SRC：12 KiB 数据；
 * - SGL：三个 DATA 条目，指向三个不连续的 4 KiB 页。
 * 依次执行：H2D 把 SRC 写入设备内存；D2H（FENCE）读回到 DST1 + 4（4 B
 * 对齐、跨 4 KiB）；D2H（FENCE、NOTIFY）经 SGL 写到三个页；一个 DEV_ADDR
 * 越界的 H2D 使通道错误停止，RESUME 后再做一次小的 H2D。
 *
 * @see spec-host-dma
 */

#include <stdint.h>

#include "gw100_hdma.h"
#include "gw100_mgmt.h"

/** 约定的 host 缓冲（baremetal 的 BM_DMA_BUF_BASE） */
#define HOST_BUF    0x90000000UL
/** 源数据 */
#define H_SRC       (HOST_BUF + 0x00000)
/** 连续的目的缓冲 */
#define H_DST1      (HOST_BUF + 0x10000)
/** SGL：一段，三个 DATA 条目 */
#define H_SGL       (HOST_BUF + 0x20000)
/** 测试数据的长度 */
#define LEN         12288u
/** 设备内存中的缓冲 */
#define DPA_BUF     0x100000UL
/** 设备内存之外的 DPA：32 GiB */
#define DPA_BAD     0x800000000UL
/** 等待一次完成的超时：1 ms */
#define TIMEOUT     100000UL

/** 写一个描述符 */
static unsigned
put(struct gw_hdma_chan *c, uint32_t dw0, uint32_t len, uint64_t host,
    uint64_t dev, uint32_t dw6)
{
    struct gw_hdma_desc d = {dw0, len, host, dev, dw6, 0};
    return gw_hdma_put(c, &d);
}

/**
 * 运行 HDMA 的测试，c 为已初始化的数据通道；返回 0 表示通过，否则为
 * 失败的步骤号
 */
int
hdma_selftest(struct gw_hdma_chan *c)
{
    unsigned i0 = put(c, GW_DESC_H2D, LEN, H_SRC, DPA_BUF, 0);
    unsigned i1 = put(c, GW_DESC_D2H | GW_DESC_FENCE, LEN, H_DST1 + 4,
                      DPA_BUF, 0);
    unsigned i2 = put(c, GW_DESC_D2H | GW_DESC_SGL | GW_DESC_FENCE |
                         GW_DESC_NOTIFY, LEN, H_SGL, DPA_BUF, 48);
    gw_hdma_commit(c);
    if (gw_hdma_wait(c, i0, TIMEOUT) != 0)
        return 1;
    if (gw_hdma_wait(c, i1, TIMEOUT) != 0)
        return 2;
    if (gw_hdma_wait(c, i2, TIMEOUT) != 0)
        return 3;

    /* DEV_ADDR 越界：描述符以错误码 5 结束，通道错误停止 */
    gw_write32(GW_EVT_PENDING, GW_EVT_HDMA_ERR);
    unsigned i3 = put(c, GW_DESC_H2D, 64, H_SRC, DPA_BAD, 0);
    gw_hdma_commit(c);
    if (gw_hdma_wait(c, i3, TIMEOUT) != 5)
        return 4;
    while ((gw_read32(GW_HDMA_CH(c->ch, GW_CH_STATUS)) & 3) !=
           GW_CH_ERRSTOP)
        ;
    uint32_t err = gw_read32(GW_HDMA_CH(c->ch, GW_CH_ERR));
    if ((err & 0xFF) != 5 || ((err >> 8) & 3) != 0 || (err >> 16) != i3)
        return 5;
    while (!(gw_read32(GW_EVT_PENDING) & GW_EVT_HDMA_ERR))
        ;
    gw_write32(GW_EVT_PENDING, GW_EVT_HDMA_ERR);

    /* RESUME 之后继续处理 */
    gw_write32(GW_HDMA_CH(c->ch, GW_CH_CTRL),
               1u | (1u << 8) | GW_CH_RESUME);
    unsigned i4 = put(c, GW_DESC_H2D | GW_DESC_NOTIFY, 64, H_SRC,
                      DPA_BUF + LEN, 0);
    gw_hdma_commit(c);
    if (gw_hdma_wait(c, i4, TIMEOUT) != 0)
        return 6;
    return 0;
}
