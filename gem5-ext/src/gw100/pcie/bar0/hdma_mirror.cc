/**
 * @file  hdma_mirror.cc
 * @brief HdmaMirror 的实现
 *
 * @see spec-host-dma, spec-pcie
 */

#include "src/gw100/pcie/bar0/hdma_mirror.hh"

namespace gem5
{
namespace gw100
{

/** 读 BAR0 偏移 off 起的 size 字节 */
void
HdmaMirror::read(Addr off, uint8_t *data, unsigned size)
{
    // 按 32 位字读取（64 位读先低后高，低位的读锁存高位）
    for (unsigned i = 0; i < size;) {
        Addr b = off + i;
        Addr w = b & ~Addr(3);
        uint32_t v = 0;
        hdma.read(w - Base, v, true);
        for (unsigned k = b - w; k < 4 && i < size; k++, i++)
            data[i] = uint8_t(v >> (8 * k));
    }
}

} // namespace gw100
} // namespace gem5
