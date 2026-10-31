/**
 * @file  pci.c
 * @brief PCIe 配置空间访问、设备查找与 BAR 分配的实现
 */

#include "bm/mmio.h"
#include "bm/pci.h"
#include "bm/platform.h"

/** d 的配置空间 off 处在 ECAM 中的 CPU 地址 */
static uintptr_t
cfg_addr(bm_pci_bdf_t d, unsigned off)
{
    return BM_PCI_ECAM_BASE + ((uintptr_t)d.bus << 20) +
           ((uintptr_t)d.dev << 15) + ((uintptr_t)d.fn << 12) + off;
}

/** 读 d 的配置空间 off 处的 8 位值 */
uint8_t
bm_pci_cfg_read8(bm_pci_bdf_t d, unsigned off)
{
    return bm_read8(cfg_addr(d, off));
}

/** 读 d 的配置空间 off 处的 16 位值 */
uint16_t
bm_pci_cfg_read16(bm_pci_bdf_t d, unsigned off)
{
    return bm_read16(cfg_addr(d, off));
}

/** 读 d 的配置空间 off 处的 32 位值 */
uint32_t
bm_pci_cfg_read32(bm_pci_bdf_t d, unsigned off)
{
    return bm_read32(cfg_addr(d, off));
}

/** 写 d 的配置空间 off 处的 8 位值 */
void
bm_pci_cfg_write8(bm_pci_bdf_t d, unsigned off, uint8_t v)
{
    bm_write8(cfg_addr(d, off), v);
}

/** 写 d 的配置空间 off 处的 16 位值 */
void
bm_pci_cfg_write16(bm_pci_bdf_t d, unsigned off, uint16_t v)
{
    bm_write16(cfg_addr(d, off), v);
}

/** 写 d 的配置空间 off 处的 32 位值 */
void
bm_pci_cfg_write32(bm_pci_bdf_t d, unsigned off, uint32_t v)
{
    bm_write32(cfg_addr(d, off), v);
}

/** 在 0 号总线上查找设备；找到返回 0 并填写 *out，找不到返回 -1 */
int
bm_pci_find(uint16_t vendor, uint16_t device, bm_pci_bdf_t *out)
{
    for (unsigned dev = 0; dev < 32; dev++) {
        for (unsigned fn = 0; fn < 8; fn++) {
            bm_pci_bdf_t d = { 0, (uint8_t)dev, (uint8_t)fn };
            uint16_t vid = bm_pci_cfg_read16(d, BM_PCI_VENDOR_ID);
            if (vid == 0xFFFF) {
                if (fn == 0)
                    break;          /* 该设备号上没有设备 */
                continue;
            }
            if (vid == vendor &&
                bm_pci_cfg_read16(d, BM_PCI_DEVICE_ID) == device) {
                *out = d;
                return 0;
            }
            /* 单功能设备只看功能 0 */
            if (fn == 0 && !(bm_pci_cfg_read8(d, BM_PCI_HEADER_TYPE) & 0x80))
                break;
        }
    }
    return -1;
}

/** 探测 BAR 的类型和大小；探测期间暂时关闭内存解码，结束后恢复 */
bm_pci_bar_t
bm_pci_bar_probe(bm_pci_bdf_t d, unsigned bar)
{
    bm_pci_bar_t r = { 0, 0, 0, 0 };
    unsigned off = BM_PCI_BAR0 + 4 * bar;
    uint16_t cmd = bm_pci_cfg_read16(d, BM_PCI_COMMAND);

    bm_pci_cfg_write16(d, BM_PCI_COMMAND,
                       cmd & ~(uint16_t)BM_PCI_CMD_MEM);

    uint32_t orig = bm_pci_cfg_read32(d, off);
    bm_pci_cfg_write32(d, off, 0xFFFFFFFFu);
    uint32_t lo = bm_pci_cfg_read32(d, off);
    bm_pci_cfg_write32(d, off, orig);

    if (lo == 0)
        goto out;
    if (lo & 1) {
        r.is_io = 1;
        r.size = (uint32_t)(~(lo & ~3u) + 1);
        goto out;
    }

    r.prefetchable = (lo >> 3) & 1;
    uint64_t mask = lo & ~0xFull;
    if (((lo >> 1) & 3) == 2) {
        r.is_64bit = 1;
        uint32_t orig_hi = bm_pci_cfg_read32(d, off + 4);
        bm_pci_cfg_write32(d, off + 4, 0xFFFFFFFFu);
        uint32_t hi = bm_pci_cfg_read32(d, off + 4);
        bm_pci_cfg_write32(d, off + 4, orig_hi);
        mask |= (uint64_t)hi << 32;
        r.size = ~mask + 1;
    } else {
        r.size = (uint32_t)(~(uint32_t)mask + 1);
    }

out:
    bm_pci_cfg_write16(d, BM_PCI_COMMAND, cmd);
    return r;
}

/** 下一个可分配给 32 位 BAR 的总线地址 */
static uint64_t next_bus32 = BM_PCI_MEM32_BUS_BASE;
/** 下一个可分配给 64 位 BAR 的总线地址 */
static uint64_t next_bus64 = BM_PCI_MEM64_BUS_BASE;

/**
 * 为内存 BAR 分配总线地址并写入，返回 CPU 访问该 BAR 的地址。
 * 32 位 BAR 和 64 位 BAR 分别从各自的地址范围按大小对齐分配。
 * BAR 未实现、是 I/O BAR 或地址范围用尽时返回 0。
 */
uintptr_t
bm_pci_bar_assign(bm_pci_bdf_t d, unsigned bar)
{
    bm_pci_bar_t b = bm_pci_bar_probe(d, bar);
    if (b.size == 0 || b.is_io)
        return 0;

    uint64_t *next = b.is_64bit ? &next_bus64 : &next_bus32;
    uint64_t end = b.is_64bit ? BM_PCI_MEM64_BUS_END : BM_PCI_MEM32_BUS_END;
    uint64_t addr = (*next + b.size - 1) & ~(b.size - 1);
    if (addr + b.size > end)
        return 0;
    *next = addr + b.size;

    unsigned off = BM_PCI_BAR0 + 4 * bar;
    bm_pci_cfg_write32(d, off, (uint32_t)addr);
    if (b.is_64bit)
        bm_pci_cfg_write32(d, off + 4, (uint32_t)(addr >> 32));
    return BM_PCI_MEM_CPU_BASE + addr;
}

/** 置位 Command 寄存器中的 bits */
void
bm_pci_enable(bm_pci_bdf_t d, uint16_t bits)
{
    uint16_t cmd = bm_pci_cfg_read16(d, BM_PCI_COMMAND);
    bm_pci_cfg_write16(d, BM_PCI_COMMAND, cmd | bits);
}

/**
 * 沿 capability 链查找 id，返回它在配置空间中的偏移；找不到返回 0。
 * 链表过长（成环）时也返回 0。
 */
unsigned
bm_pci_find_cap(bm_pci_bdf_t d, uint8_t id)
{
    if (!(bm_pci_cfg_read16(d, BM_PCI_STATUS) & BM_PCI_STATUS_CAP_LIST))
        return 0;
    unsigned off = bm_pci_cfg_read8(d, BM_PCI_CAP_PTR) & ~3u;
    /* 标准配置空间最多容纳 48 个 capability，超过即视为成环 */
    for (int n = 0; off >= 0x40 && n < 48; n++) {
        if (bm_pci_cfg_read8(d, off) == id)
            return off;
        off = bm_pci_cfg_read8(d, off + 1) & ~3u;
    }
    return 0;
}
