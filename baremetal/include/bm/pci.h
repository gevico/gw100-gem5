/**
 * @file  pci.h
 * @brief PCIe 配置空间访问、设备查找与 BAR 分配
 *
 * 裸机环境没有固件替我们枚举 PCIe，测试自己扫描 ECAM、探测 BAR 大小
 * 并分配地址。地址范围见 platform.h。只扫描 0 号总线。
 */

#ifndef BM_PCI_H
#define BM_PCI_H

#include <stdint.h>

/* 配置空间中的常用偏移 */
#define BM_PCI_VENDOR_ID        0x00
#define BM_PCI_DEVICE_ID        0x02
#define BM_PCI_COMMAND          0x04
#define BM_PCI_STATUS           0x06
#define BM_PCI_REVISION_ID      0x08
#define BM_PCI_CLASS_PROG       0x09
#define BM_PCI_CLASS_SUB        0x0A
#define BM_PCI_CLASS_BASE       0x0B
#define BM_PCI_HEADER_TYPE      0x0E
#define BM_PCI_BAR0             0x10
#define BM_PCI_INTERRUPT_PIN    0x3D

/* Command 寄存器的位 */
#define BM_PCI_CMD_MEM          (1u << 1)
#define BM_PCI_CMD_MASTER       (1u << 2)
#define BM_PCI_CMD_INTX_DISABLE (1u << 10)

typedef struct {
    uint8_t bus;
    uint8_t dev;
    uint8_t fn;
} bm_pci_bdf_t;

/* BAR 的探测结果 */
typedef struct {
    uint64_t size;      /* 字节；0 表示该 BAR 未实现 */
    int is_io;          /* I/O BAR */
    int is_64bit;       /* 64 位内存 BAR，占用两个 BAR 槽位 */
    int prefetchable;
} bm_pci_bar_t;

uint8_t  bm_pci_cfg_read8(bm_pci_bdf_t d, unsigned off);
uint16_t bm_pci_cfg_read16(bm_pci_bdf_t d, unsigned off);
uint32_t bm_pci_cfg_read32(bm_pci_bdf_t d, unsigned off);
void bm_pci_cfg_write8(bm_pci_bdf_t d, unsigned off, uint8_t v);
void bm_pci_cfg_write16(bm_pci_bdf_t d, unsigned off, uint16_t v);
void bm_pci_cfg_write32(bm_pci_bdf_t d, unsigned off, uint32_t v);

/* 在 0 号总线上查找设备；找到返回 0 并填写 *out，找不到返回 -1 */
int bm_pci_find(uint16_t vendor, uint16_t device, bm_pci_bdf_t *out);

/* 探测 BAR 的类型和大小；探测期间暂时关闭内存解码，结束后恢复 */
bm_pci_bar_t bm_pci_bar_probe(bm_pci_bdf_t d, unsigned bar);

/*
 * 为内存 BAR 分配总线地址并写入，返回 CPU 访问该 BAR 的地址。
 * 32 位 BAR 和 64 位 BAR 分别从各自的地址范围按大小对齐分配。
 * BAR 未实现、是 I/O BAR 或地址范围用尽时返回 0。
 */
uintptr_t bm_pci_bar_assign(bm_pci_bdf_t d, unsigned bar);

/* 置位 Command 寄存器中的 bits */
void bm_pci_enable(bm_pci_bdf_t d, uint16_t bits);

#endif /* BM_PCI_H */
