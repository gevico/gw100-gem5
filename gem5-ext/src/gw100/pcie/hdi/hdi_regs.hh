/**
 * @file  hdi_regs.hh
 * @brief BAR0 的 HDI 控制器寄存器块（0x5000）与 doorbell 页（0x80000 起）
 *
 * 状态在 HdiCtrl 中，这些块只是它在 BAR0 中的视图。HDI 控制器寄存器
 * 未定义的偏移是保留偏移，由 Bar0Handler 按 spec-pcie §5.2 处理；
 * doorbell 页只有 SQ_TAIL、CQ_HEAD 两个寄存器，读回 0（spec-hdi §4.2）。
 *
 * @see spec-hdi, spec-pcie
 */

#ifndef __GW100_PCIE_HDI_HDI_REGS_HH__
#define __GW100_PCIE_HDI_HDI_REGS_HH__

#include "src/gw100/pcie/hdi/hdi_ctrl.hh"
#include "src/gw100/pcie/pcie_types.hh"

namespace gem5
{
namespace gw100
{

/** BAR0 的 HDI 控制器寄存器块 */
class HdiRegsBlock : public RegBlock
{
  public:
    /** 本块在 BAR0 中的起始偏移 */
    static constexpr Addr Base = 0x5000;

    /** 构造：hdi 为 HDI 控制器的状态 */
    explicit HdiRegsBlock(HdiCtrl &hdi) : hdi(hdi) {}

    /** 块在 BAR0 中的起始偏移 */
    Addr base() const override { return Base; }

    /** 偏移 off 是否是已定义的寄存器 */
    bool implemented(Addr off) const override
    { return HdiCtrl::defined(off - Base); }

    /** 读 off 起的 size 字节 */
    void read(Addr off, uint8_t *data, unsigned size) override;

    /** 写 off 起的 size 字节（4 或 8） */
    void write(Addr off, const uint8_t *data, unsigned size) override;

    /** 状态在 HdiCtrl 中，由它复位 */
    void reset() override {}

    /** 状态在 HdiCtrl 中 */
    void serialize(CheckpointOut &) const override {}

    /** 状态在 HdiCtrl 中 */
    void unserialize(CheckpointIn &) override {}

  private:
    /** HDI 控制器的状态 */
    HdiCtrl &hdi;
};

/** BAR0 的一个 doorbell 页 */
class DoorbellPage : public RegBlock
{
  public:
    /** 管理队列 doorbell 页在 BAR0 中的偏移 */
    static constexpr Addr Base0 = 0x80000;

    /** 构造：hdi 为 HDI 控制器的状态，page 为页号（0 是管理队列） */
    DoorbellPage(HdiCtrl &hdi, unsigned page) : hdi(hdi), page(page) {}

    /** 块在 BAR0 中的起始偏移 */
    Addr base() const override { return Base0 + 0x1000 * page; }

    /** 只有 SQ_TAIL（0x000）和 CQ_HEAD（0x004） */
    bool implemented(Addr off) const override
    { return off - base() < 8; }

    /** doorbell 只写：读回 0 */
    void read(Addr off, uint8_t *data, unsigned size) override;

    /** 写 doorbell；必须按 32 位写入 */
    void write(Addr off, const uint8_t *data, unsigned size) override;

    /** 状态在 HdiCtrl 中，由它复位 */
    void reset() override {}

    /** 状态在 HdiCtrl 中 */
    void serialize(CheckpointOut &) const override {}

    /** 状态在 HdiCtrl 中 */
    void unserialize(CheckpointIn &) override {}

  private:
    /** HDI 控制器的状态 */
    HdiCtrl &hdi;
    /** 页号 */
    const unsigned page;
};

} // namespace gw100
} // namespace gem5

#endif // __GW100_PCIE_HDI_HDI_REGS_HH__
