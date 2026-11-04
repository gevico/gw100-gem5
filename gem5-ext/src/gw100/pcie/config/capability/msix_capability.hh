/**
 * @file  msix_capability.hh
 * @brief MSI-X capability：表与 PBA 都在 BAR0
 *
 * Message Control 的 Table Size 为表大小减 1，Function Mask 与 MSI-X
 * Enable 可写；表在 BAR0 偏移 0x2000，PBA 在 0x3000（spec-pcie §5.1、
 * §8.2）。Enable、Function Mask 的作用由 IntCtrl 实现，所属设备在配置写
 * 之后把它们交给 IntCtrl。
 *
 * @see spec-pcie
 */

#ifndef __GW100_PCIE_CONFIG_CAPABILITY_MSIX_CAPABILITY_HH__
#define __GW100_PCIE_CONFIG_CAPABILITY_MSIX_CAPABILITY_HH__

#include "src/gw100/pcie/config/capability/capability.hh"

namespace gem5
{
namespace gw100
{

/** MSI-X capability */
class MsixCapability : public Capability
{
  public:
    /** Message Control 相对 capability 起始的偏移 */
    static constexpr Addr MsgCtrl = 0x02;
    /** Message Control 的 Function Mask */
    static constexpr uint16_t FuncMask = 1u << 14;
    /** Message Control 的 MSI-X Enable */
    static constexpr uint16_t Enable = 1u << 15;

    /** 构造：table_size 为表项个数，表和 PBA 位于 BAR0 的给定偏移 */
    MsixCapability(unsigned table_size, uint32_t table_off,
                   uint32_t pba_off)
        : tableSize(table_size), tableOff(table_off), pbaOff(pba_off) {}

    /** capability ID：MSI-X */
    uint8_t id() const override { return 0x11; }

    /** 写 Message Control、表与 PBA 的位置；Function Mask、Enable 可写 */
    void init(CfgBytes &cfg, CfgBytes &wmask, CfgBytes &w1c,
              Addr base) const override;

    /** 没有需要设备立即处理的事件 */
    CfgEvent afterWrite(const CfgBytes &, CfgBytes &, Addr, Addr, unsigned,
                        uint32_t) const override
    { return CfgEvent::None; }

  private:
    /** 表项个数 */
    const unsigned tableSize;
    /** 表在 BAR0 中的偏移 */
    const uint32_t tableOff;
    /** PBA 在 BAR0 中的偏移 */
    const uint32_t pbaOff;
};

} // namespace gw100
} // namespace gem5

#endif // __GW100_PCIE_CONFIG_CAPABILITY_MSIX_CAPABILITY_HH__
