/**
 * @file  msix_regs.hh
 * @brief BAR0 的 MSI-X 表（0x2000）与 PBA（0x3000）
 *
 * 表项 16 B：Message Address 低、高 32 位，Message Data，Vector Control
 * （[0] Mask，复位值 1）。PBA 只读，向量 0 – 31 在第一个 32 位字。访问
 * 宽度按 PCIe 规范为 DWORD 或 QWORD（spec-pcie §5.2、§8.2）。状态在
 * IntCtrl 中，这两个块只是它在 BAR0 中的视图。
 *
 * @see spec-pcie
 */

#ifndef __GW100_PCIE_INTR_MSIX_REGS_HH__
#define __GW100_PCIE_INTR_MSIX_REGS_HH__

#include "src/gw100/pcie/intr/int_ctrl.hh"
#include "src/gw100/pcie/pcie_types.hh"

namespace gem5
{
namespace gw100
{

/** MSI-X 表 */
class MsixTable : public RegBlock
{
  public:
    /** 本块在 BAR0 中的起始偏移 */
    static constexpr Addr Base = 0x2000;

    /** 构造：ic 为中断控制器 */
    explicit MsixTable(IntCtrl &ic) : ic(ic) {}

    /** 块在 BAR0 中的起始偏移 */
    Addr base() const override { return Base; }

    /** 偏移 off 是否落在表项范围内 */
    bool implemented(Addr off) const override
    { return off >= Base && off < Base + 16 * ic.vectors(); }

    /** 读 off 起的 size 字节 */
    void read(Addr off, uint8_t *data, unsigned size) override;

    /** 写 off 起的 size 字节（4 或 8） */
    void write(Addr off, const uint8_t *data, unsigned size) override;

    /** 状态在 IntCtrl 中，由它复位 */
    void reset() override {}

    /** 状态在 IntCtrl 中 */
    void serialize(CheckpointOut &) const override {}

    /** 状态在 IntCtrl 中 */
    void unserialize(CheckpointIn &) override {}

  private:
    /** 中断控制器 */
    IntCtrl &ic;
};

/** MSI-X PBA */
class MsixPba : public RegBlock
{
  public:
    /** 本块在 BAR0 中的起始偏移 */
    static constexpr Addr Base = 0x3000;

    /** 构造：ic 为中断控制器 */
    explicit MsixPba(IntCtrl &ic) : ic(ic) {}

    /** 块在 BAR0 中的起始偏移 */
    Addr base() const override { return Base; }

    /** 一个 QWORD：0x3000、0x3004 */
    bool implemented(Addr off) const override
    { return off >= Base && off < Base + 8; }

    /** 读 off 起的 size 字节 */
    void read(Addr off, uint8_t *data, unsigned size) override;

    /** 只读：写入被忽略 */
    void write(Addr, const uint8_t *, unsigned) override {}

    /** 状态在 IntCtrl 中，由它复位 */
    void reset() override {}

    /** 状态在 IntCtrl 中 */
    void serialize(CheckpointOut &) const override {}

    /** 状态在 IntCtrl 中 */
    void unserialize(CheckpointIn &) override {}

  private:
    /** 中断控制器 */
    IntCtrl &ic;
};

} // namespace gw100
} // namespace gem5

#endif // __GW100_PCIE_INTR_MSIX_REGS_HH__
