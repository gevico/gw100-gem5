/**
 * @file  msix_capability.cc
 * @brief MsixCapability 的实现
 *
 * @see spec-pcie
 */

#include "src/gw100/pcie/config/capability/msix_capability.hh"

namespace gem5
{
namespace gw100
{

/** 写 Message Control、表与 PBA 的位置；Function Mask、Enable 可写 */
void
MsixCapability::init(CfgBytes &cfg, CfgBytes &wmask, CfgBytes &w1c,
                     Addr base) const
{
    (void)w1c;
    put16(cfg, base + MsgCtrl, uint16_t(tableSize - 1));
    wmask[base + MsgCtrl + 1] = 0xC0;   // [14] Function Mask，[15] Enable
    // BIR = 0：表和 PBA 都在 BAR0
    for (unsigned i = 0; i < 4; i++) {
        cfg[base + 0x04 + i] = uint8_t(tableOff >> (8 * i));
        cfg[base + 0x08 + i] = uint8_t(pbaOff >> (8 * i));
    }
}

} // namespace gw100
} // namespace gem5
