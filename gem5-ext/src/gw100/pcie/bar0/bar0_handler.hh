/**
 * @file  bar0_handler.hh
 * @brief BAR0 的前端：访问规则与按寄存器块分派
 *
 * 统一执行 spec-pcie §5.2 的访问规则：宽度与对齐、保留偏移、第一次错误
 * 锁存，任何访问都以成功完成结束。合法访问按 4 KiB 块分派给登记的
 * RegBlock；以后的 MSI-X、HDI、HDMA 镜像、doorbell 只需登记新的块。
 *
 * @see spec-pcie
 */

#ifndef __GW100_PCIE_BAR0_BAR0_HANDLER_HH__
#define __GW100_PCIE_BAR0_BAR0_HANDLER_HH__

#include <map>

#include "base/types.hh"
#include "src/gw100/pcie/pcie_types.hh"
#include "src/gw100/pcie/status/status_unit.hh"

namespace gem5
{
namespace gw100
{

/** BAR0 的处理者（spec-pcie §5） */
class Bar0Handler : public BarHandler
{
  public:
    /** 每个寄存器块的大小 */
    static constexpr Addr BlockSize = 0x1000;

    /** 构造；错误记录到 status，每次访问的延迟为 pio_delay */
    Bar0Handler(StatusUnit &status, Tick pio_delay)
        : status(status), pioDelay(pio_delay)
    {}

    /** 登记一个寄存器块；块的起始偏移必须按 BlockSize 对齐且互不重复 */
    void addBlock(RegBlock &block);

    /**
     * 处理一次访问：先按 spec-pcie §5.2 检查宽度和偏移，出错时记录并
     * 以成功完成结束；合法访问分派到对应的寄存器块
     */
    Tick access(BarAccess &a) override;

  private:
    /** [off, off + size) 覆盖的每个 32 位字是否都是已实现的寄存器 */
    bool implemented(Addr off, unsigned size) const;

    /** BAR0 偏移 off 所在的寄存器块；没有登记时返回 nullptr */
    RegBlock *blockAt(Addr off) const;

    /** 记录错误的状态单元 */
    StatusUnit &status;
    /** 每次访问的设备侧延迟 */
    Tick pioDelay;
    /** 已登记的寄存器块，按起始偏移索引 */
    std::map<Addr, RegBlock *> blocks;
};

} // namespace gw100
} // namespace gem5

#endif // __GW100_PCIE_BAR0_BAR0_HANDLER_HH__
