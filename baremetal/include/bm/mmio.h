/**
 * @file  mmio.h
 * @brief MMIO 读写与内存屏障
 *
 * 写之前、读之后各加一条屏障，语义与 Linux 的 writel/readl 相同：
 * 之前的内存写入在 MMIO 写之前对设备可见；MMIO 读返回后，之后的
 * 内存读取不会提前。访问地址必须自然对齐。
 */

#ifndef BM_MMIO_H
#define BM_MMIO_H

#include <stdint.h>

/** 数据同步屏障：屏障之前的访存全部完成后才继续执行 */
static inline void bm_dsb(void) { __asm__ volatile("dsb sy" ::: "memory"); }

/** 数据内存屏障：保证屏障前后访存的先后顺序 */
static inline void bm_dmb(void) { __asm__ volatile("dmb sy" ::: "memory"); }

/** 读 addr 处的 8 位寄存器，读后加屏障 */
static inline uint8_t
bm_read8(uintptr_t addr)
{
    uint8_t v = *(volatile uint8_t *)addr;
    bm_dmb();
    return v;
}

/** 读 addr 处的 16 位寄存器，读后加屏障 */
static inline uint16_t
bm_read16(uintptr_t addr)
{
    uint16_t v = *(volatile uint16_t *)addr;
    bm_dmb();
    return v;
}

/** 读 addr 处的 32 位寄存器，读后加屏障 */
static inline uint32_t
bm_read32(uintptr_t addr)
{
    uint32_t v = *(volatile uint32_t *)addr;
    bm_dmb();
    return v;
}

/** 读 addr 处的 64 位寄存器，读后加屏障 */
static inline uint64_t
bm_read64(uintptr_t addr)
{
    uint64_t v = *(volatile uint64_t *)addr;
    bm_dmb();
    return v;
}

/** 写 addr 处的 8 位寄存器，写前加屏障 */
static inline void
bm_write8(uintptr_t addr, uint8_t v)
{
    bm_dmb();
    *(volatile uint8_t *)addr = v;
}

/** 写 addr 处的 16 位寄存器，写前加屏障 */
static inline void
bm_write16(uintptr_t addr, uint16_t v)
{
    bm_dmb();
    *(volatile uint16_t *)addr = v;
}

/** 写 addr 处的 32 位寄存器，写前加屏障 */
static inline void
bm_write32(uintptr_t addr, uint32_t v)
{
    bm_dmb();
    *(volatile uint32_t *)addr = v;
}

/** 写 addr 处的 64 位寄存器，写前加屏障 */
static inline void
bm_write64(uintptr_t addr, uint64_t v)
{
    bm_dmb();
    *(volatile uint64_t *)addr = v;
}

#endif /* BM_MMIO_H */
