/**
 * @file  msix_regs.cc
 * @brief MsixTable、MsixPba 的实现
 *
 * @see spec-pcie
 */

#include "src/gw100/pcie/intr/msix_regs.hh"

namespace gem5
{
namespace gw100
{

/** 读 off 起的 size 字节 */
void
MsixTable::read(Addr off, uint8_t *data, unsigned size)
{
    for (unsigned i = 0; i < size; i++) {
        Addr b = off + i - Base;
        uint32_t w = ic.tableRead(b / 16, (b % 16) / 4);
        data[i] = uint8_t(w >> (8 * (b % 4)));
    }
}

/** 写 off 起的 size 字节（4 或 8） */
void
MsixTable::write(Addr off, const uint8_t *data, unsigned size)
{
    for (unsigned i = 0; i + 4 <= size; i += 4) {
        Addr b = off + i - Base;
        uint32_t w = data[i] | (data[i + 1] << 8) | (data[i + 2] << 16) |
                     (uint32_t(data[i + 3]) << 24);
        ic.tableWrite(b / 16, (b % 16) / 4, w);
    }
}

/** 读 off 起的 size 字节 */
void
MsixPba::read(Addr off, uint8_t *data, unsigned size)
{
    for (unsigned i = 0; i < size; i++) {
        Addr b = off + i - Base;
        data[i] = b < 4 ? uint8_t(ic.pba() >> (8 * b)) : 0;
    }
}

} // namespace gw100
} // namespace gem5
