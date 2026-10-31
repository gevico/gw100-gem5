/**
 * @file  window_table.hh
 * @brief BAR2 调试窗口的配置与地址转换
 *
 * 保存全部窗口的 WIN_CTRL 与 WIN_BASE，负责寄存器字段的掩码，以及把
 * BAR2 偏移转换为 DPA 并检查使能、目标范围和只读。窗口配置寄存器块
 * （WindowRegs）和 BAR2 处理者（Bar2Handler）都经本类访问窗口。
 *
 * @see spec-pcie
 */

#ifndef __GW100_PCIE_WINDOW_WINDOW_TABLE_HH__
#define __GW100_PCIE_WINDOW_WINDOW_TABLE_HH__

#include <cstdint>
#include <vector>

#include "base/types.hh"
#include "sim/serialize.hh"

namespace gem5
{
namespace gw100
{

/** 全部 BAR2 窗口的配置与地址转换（spec-pcie §7.4 – §7.6） */
class WindowTable : public Serializable
{
  public:
    /** WIN_CTRL.EN：窗口使能 */
    static constexpr uint32_t CtrlEn = 1u << 0;
    /** WIN_CTRL.RD_ONLY：窗口只读 */
    static constexpr uint32_t CtrlRdOnly = 1u << 1;

    /** 一次转换失败的原因 */
    enum class Fault
    {
        /** 转换成功 */
        None,
        /** 窗口未使能 */
        Disabled,
        /** 目标不在设备内存内，或写了只读窗口 */
        Target,
    };

    /** 一次转换的结果 */
    struct Result
    {
        /** 失败的原因；None 表示成功 */
        Fault fault;
        /** 转换得到的 DPA；只在成功时有意义 */
        Addr target;
    };

    /**
     * 构造 count 个大小为 win_size 的窗口；设备内存为
     * [devmem_base, devmem_base + devmem_size)，DPA 宽 pa_bits 位
     */
    WindowTable(unsigned count, Addr win_size, Addr devmem_base,
                Addr devmem_size, unsigned pa_bits);

    /** 窗口个数 */
    unsigned count() const { return wins.size(); }

    /** 每个窗口的大小 */
    Addr winSize() const { return _winSize; }

    /** 窗口 i 的 WIN_CTRL */
    uint32_t ctrl(unsigned i) const { return wins[i].ctrl; }

    /** 窗口 i 的 WIN_BASE 低 32 位 */
    uint32_t baseLo(unsigned i) const { return uint32_t(wins[i].base); }

    /** 窗口 i 的 WIN_BASE 高 32 位 */
    uint32_t baseHi(unsigned i) const
    { return uint32_t(wins[i].base >> 32); }

    /** 写窗口 i 的 WIN_CTRL；只保留 EN 和 RD_ONLY */
    void setCtrl(unsigned i, uint32_t v);

    /** 写窗口 i 的 WIN_BASE 低 32 位；低于窗口大小的位为 RsvdZ */
    void setBaseLo(unsigned i, uint32_t v);

    /** 写窗口 i 的 WIN_BASE 高 32 位；超出 DPA 宽度的位为 RsvdZ */
    void setBaseHi(unsigned i, uint32_t v);

    /** 把 BAR2 偏移 off 起的 size 字节转换为 DPA，并检查能否访问 */
    Result translate(Addr off, unsigned size, bool write) const;

    /** 把全部窗口恢复为复位值（未使能、基址 0） */
    void reset();

    /** 保存全部窗口的配置 */
    void serialize(CheckpointOut &cp) const override;

    /** 恢复 serialize() 保存的配置 */
    void unserialize(CheckpointIn &cp) override;

  private:
    /** 一个窗口的配置 */
    struct Window
    {
        /** WIN_CTRL：EN、RD_ONLY 两位 */
        uint32_t ctrl = 0;
        /** 目标 DPA，按窗口大小对齐 */
        uint64_t base = 0;
    };

    /** 全部窗口 */
    std::vector<Window> wins;
    /** 每个窗口的大小 */
    Addr _winSize;
    /** 设备内存的起始 DPA */
    Addr devmemBase;
    /** 设备内存大小 */
    Addr devmemSize;
    /** WIN_BASE 低 32 位中可写的位 */
    uint32_t baseLoMask;
    /** WIN_BASE 高 32 位中可写的位 */
    uint32_t baseHiMask;
};

} // namespace gw100
} // namespace gem5

#endif // __GW100_PCIE_WINDOW_WINDOW_TABLE_HH__
