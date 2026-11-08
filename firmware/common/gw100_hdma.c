/**
 * @file  gw100_hdma.c
 * @brief 固件的最小 HDMA 驱动
 *
 * @see spec-host-dma
 */

#include "gw100_hdma.h"

/** 读 mtime */
static uint64_t
mtime(void)
{
    return *(volatile uint64_t *)GW_CLINT_MTIME;
}

/** 初始化通道：环在 SRAM 中，entries 为 2 的幂，使能 */
void
gw_hdma_chan_init(struct gw_hdma_chan *c, unsigned ch,
                  struct gw_hdma_desc *ring, unsigned entries)
{
    unsigned log2 = 0;
    while ((1u << log2) < entries)
        log2++;
    c->ch = ch;
    c->ring = ring;
    c->entries = entries;
    c->tail = 0;
    c->consumed = 0;
    uintptr_t base = (uintptr_t)ring;
    gw_write32(GW_HDMA_CH(ch, GW_CH_CTRL), 0);
    gw_write32(GW_HDMA_CH(ch, GW_CH_RING_LO), (uint32_t)base);
    gw_write32(GW_HDMA_CH(ch, GW_CH_RING_HI), (uint32_t)(base >> 32));
    gw_write32(GW_HDMA_CH(ch, GW_CH_RING_SIZE), log2);
    gw_write32(GW_HDMA_CH(ch, GW_CH_CTRL), 1u | (1u << 8));
}

/** 把描述符 d 写入环（DW7 清零），返回它的下标；不提交 */
unsigned
gw_hdma_put(struct gw_hdma_chan *c, const struct gw_hdma_desc *d)
{
    unsigned idx = c->tail;
    struct gw_hdma_desc *s = &c->ring[idx];
    s->dw0 = d->dw0;
    s->len = d->len;
    s->host = d->host;
    s->dev = d->dev;
    s->dw6 = d->dw6;
    s->dw7 = 0;
    c->tail = (idx + 1) & (c->entries - 1);
    return idx;
}

/** 提交已写入的描述符：写 CH_TAIL */
void
gw_hdma_commit(struct gw_hdma_chan *c)
{
    __asm__ volatile("fence w, o" ::: "memory");
    gw_write32(GW_HDMA_CH(c->ch, GW_CH_TAIL), c->tail);
}

/**
 * 等待下标 idx 的描述符完成：按 §5.3 先清 HDMA_NOTIFY 再读 CH_HEAD，
 * 超时 timeout 个 mtime 周期。返回 DW7 的 ERR_CODE，超时返回 -1
 */
int
gw_hdma_wait(struct gw_hdma_chan *c, unsigned idx, uint64_t timeout)
{
    uint64_t start = mtime();
    for (;;) {
        gw_write32(GW_EVT_PENDING, GW_EVT_HDMA_NOTIFY);
        unsigned head = gw_read32(GW_HDMA_CH(c->ch, GW_CH_HEAD));
        /* C 到 HEAD 之间的描述符都已回写；idx 在其中即完成 */
        while (c->consumed != head) {
            unsigned k = c->consumed;
            c->consumed = (k + 1) & (c->entries - 1);
            if (k == idx)
                return (int)((c->ring[k].dw7 >> 8) & 0xFF);
        }
        if (mtime() - start > timeout)
            return -1;
    }
}
