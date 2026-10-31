/**
 * @file  window_table.cc
 * @brief WindowTable 的实现
 *
 * @see spec-pcie
 */

#include "src/gw100/pcie/window/window_table.hh"

#include "base/bitfield.hh"
#include "base/logging.hh"

namespace gem5
{
namespace gw100
{

/**
 * 构造 count 个大小为 win_size 的窗口；设备内存为
 * [devmem_base, devmem_base + devmem_size)，DPA 宽 pa_bits 位
 */
WindowTable::WindowTable(unsigned count, Addr win_size, Addr devmem_base,
                         Addr devmem_size, unsigned pa_bits)
    : wins(count), _winSize(win_size), devmemBase(devmem_base),
      devmemSize(devmem_size), baseLoMask(uint32_t(~(win_size - 1))),
      baseHiMask(pa_bits >= 64 ? 0xFFFFFFFFu : uint32_t(mask(pa_bits - 32)))
{}

/** 写窗口 i 的 WIN_CTRL；只保留 EN 和 RD_ONLY */
void
WindowTable::setCtrl(unsigned i, uint32_t v)
{
    wins[i].ctrl = v & (CtrlEn | CtrlRdOnly);
}

/** 写窗口 i 的 WIN_BASE 低 32 位；低于窗口大小的位为 RsvdZ */
void
WindowTable::setBaseLo(unsigned i, uint32_t v)
{
    wins[i].base = (wins[i].base & ~mask(32)) | (v & baseLoMask);
}

/** 写窗口 i 的 WIN_BASE 高 32 位；超出 DPA 宽度的位为 RsvdZ */
void
WindowTable::setBaseHi(unsigned i, uint32_t v)
{
    wins[i].base = (wins[i].base & mask(32)) |
                   (uint64_t(v & baseHiMask) << 32);
}

/** 把 BAR2 偏移 off 起的 size 字节转换为 DPA，并检查能否访问 */
WindowTable::Result
WindowTable::translate(Addr off, unsigned size, bool write) const
{
    const Window &w = wins[off / _winSize];
    const Addr target = w.base + off % _winSize;
    if (!(w.ctrl & CtrlEn))
        return {Fault::Disabled, target};
    if (target < devmemBase || target + size > devmemBase + devmemSize)
        return {Fault::Target, target};
    if (write && (w.ctrl & CtrlRdOnly))
        return {Fault::Target, target};
    return {Fault::None, target};
}

/** 把全部窗口恢复为复位值（未使能、基址 0） */
void
WindowTable::reset()
{
    for (auto &w : wins)
        w = Window{};
}

/** 保存全部窗口的配置 */
void
WindowTable::serialize(CheckpointOut &cp) const
{
    std::vector<uint32_t> win_ctrl;
    std::vector<uint64_t> win_base;
    for (const auto &w : wins) {
        win_ctrl.push_back(w.ctrl);
        win_base.push_back(w.base);
    }
    SERIALIZE_CONTAINER(win_ctrl);
    SERIALIZE_CONTAINER(win_base);
}

/** 恢复 serialize() 保存的配置 */
void
WindowTable::unserialize(CheckpointIn &cp)
{
    std::vector<uint32_t> win_ctrl;
    std::vector<uint64_t> win_base;
    UNSERIALIZE_CONTAINER(win_ctrl);
    UNSERIALIZE_CONTAINER(win_base);
    fatal_if(win_ctrl.size() != wins.size() ||
             win_base.size() != wins.size(),
             "WindowTable: checkpoint 窗口数不符");
    for (size_t i = 0; i < wins.size(); i++)
        wins[i] = Window{win_ctrl[i], win_base[i]};
}

} // namespace gw100
} // namespace gem5
