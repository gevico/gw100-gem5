/**
 * @file  window_regs.hh
 * @brief BAR0 的 BAR2 窗口配置寄存器块（0x1000）
 *
 * 窗口 i 的寄存器位于 0x1000 + 0x10 × i：WIN_CTRL（+0x0）、保留字
 * （+0x4）、WIN_BASE_LO（+0x8）、WIN_BASE_HI（+0xC）。状态保存在
 * WindowTable 中，本块只做寄存器解码。
 *
 * @see spec-pcie
 */

#ifndef __GW100_PCIE_BAR0_WINDOW_REGS_HH__
#define __GW100_PCIE_BAR0_WINDOW_REGS_HH__

#include <cstdint>

#include "base/types.hh"
#include "src/gw100/pcie/pcie_types.hh"
#include "src/gw100/pcie/window/window_table.hh"

namespace gem5
{
namespace gw100
{

/** BAR2 窗口配置寄存器（spec-pcie §7.5） */
class WindowRegs : public RegBlock
{
  public:
    /** 本块在 BAR0 中的起始偏移 */
    static constexpr Addr Base = 0x1000;
    /** 相邻两个窗口配置之间的间隔 */
    static constexpr Addr Stride = 0x10;

    /** 构造；窗口的状态保存在 table 中 */
    explicit WindowRegs(WindowTable &table) : table(table) {}

    /** 本块在 BAR0 中的起始偏移 */
    Addr base() const override { return Base; }

    /** BAR0 偏移 off 处的 32 位字是否属于某个已实现的窗口 */
    bool implemented(Addr off) const override
    { return off >= Base && off - Base < table.count() * Stride; }

    /** 读 BAR0 偏移 off 起的 size 字节到 data；按 32 位字逐个解码 */
    void read(Addr off, uint8_t *data, unsigned size) override;

    /** 把 data 中的 size 字节写到 BAR0 偏移 off 起；按 32 位字逐个解码 */
    void write(Addr off, const uint8_t *data, unsigned size) override;

    /** 窗口状态由 WindowTable 复位，本块没有自己的状态 */
    void reset() override {}

    /** 窗口状态由 WindowTable 保存，本块没有自己的状态 */
    void serialize(CheckpointOut &cp) const override {}

    /** 窗口状态由 WindowTable 恢复，本块没有自己的状态 */
    void unserialize(CheckpointIn &cp) override {}

  private:
    /** 保存窗口状态的表 */
    WindowTable &table;
};

} // namespace gw100
} // namespace gem5

#endif // __GW100_PCIE_BAR0_WINDOW_REGS_HH__
