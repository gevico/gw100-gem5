/**
 * @file  window_regs.cc
 * @brief WindowRegs 的实现
 *
 * @see spec-pcie
 */

#include "src/gw100/pcie/bar0/window_regs.hh"

#include <cstring>

#include "base/types.hh"
#include "sim/byteswap.hh"

namespace gem5
{
namespace gw100
{

/** 读 BAR0 偏移 off 起的 size 字节到 data；按 32 位字逐个解码 */
void
WindowRegs::read(Addr off, uint8_t *data, unsigned size)
{
    for (Addr w = off & ~Addr(3); w < off + size; w += 4) {
        const unsigned i = (w - Base) / Stride;
        const Addr field = (w - Base) % Stride;
        uint32_t val = 0;
        if (field == 0x0)
            val = table.ctrl(i);
        else if (field == 0x8)
            val = table.baseLo(i);
        else if (field == 0xC)
            val = table.baseHi(i);
        val = htole(val);
        for (unsigned b = 0; b < 4; b++) {
            const Addr a = w + b;
            if (a >= off && a < off + size)
                data[a - off] = reinterpret_cast<uint8_t *>(&val)[b];
        }
    }
}

/** 把 data 中的 size 字节写到 BAR0 偏移 off 起；按 32 位字逐个解码 */
void
WindowRegs::write(Addr off, const uint8_t *data, unsigned size)
{
    // 写入宽度只能是 4 或 8 字节且自然对齐（由 Bar0Handler 检查）
    for (Addr w = off; w < off + size; w += 4) {
        const unsigned i = (w - Base) / Stride;
        const Addr field = (w - Base) % Stride;
        uint32_t val;
        std::memcpy(&val, data + (w - off), 4);
        val = letoh(val);
        if (field == 0x0)
            table.setCtrl(i, val);
        else if (field == 0x8)
            table.setBaseLo(i, val);
        else if (field == 0xC)
            table.setBaseHi(i, val);
    }
}

} // namespace gw100
} // namespace gem5
