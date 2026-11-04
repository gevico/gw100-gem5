/**
 * @file  hdi_regs.cc
 * @brief HdiRegsBlock、DoorbellPage 的实现
 *
 * @see spec-hdi, spec-pcie
 */

#include "src/gw100/pcie/hdi/hdi_regs.hh"

#include <cstring>

namespace gem5
{
namespace gw100
{

namespace
{

/** 读小端的 32 位字 */
uint32_t
le32(const uint8_t *p)
{
    return p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24);
}

} // anonymous namespace

/** 读 off 起的 size 字节 */
void
HdiRegsBlock::read(Addr off, uint8_t *data, unsigned size)
{
    for (unsigned i = 0; i < size; i++) {
        Addr b = off + i - Base;
        uint32_t w = hdi.hostRead(b & ~Addr(3));
        data[i] = uint8_t(w >> (8 * (b % 4)));
    }
}

/** 写 off 起的 size 字节（4 或 8） */
void
HdiRegsBlock::write(Addr off, const uint8_t *data, unsigned size)
{
    for (unsigned i = 0; i + 4 <= size; i += 4)
        hdi.hostWrite(off + i - Base, le32(data + i));
}

/** doorbell 只写：读回 0 */
void
DoorbellPage::read(Addr, uint8_t *data, unsigned size)
{
    std::memset(data, 0, size);
}

/** 写 doorbell；必须按 32 位写入 */
void
DoorbellPage::write(Addr off, const uint8_t *data, unsigned size)
{
    if (size != 4)
        return;  // 64 位写不是合法的 doorbell 写入
    hdi.hostDoorbell(page, off - base(), le32(data));
}

} // namespace gw100
} // namespace gem5
