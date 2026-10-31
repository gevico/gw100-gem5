/**
 * @file  bar2_handler.hh
 * @brief BAR2 的处理者：调试窗口
 *
 * 把 BAR2 偏移经 WindowTable 转换为 DPA，检查通过后经 DevmemBackend 访问
 * 设备内存，返回固定延迟（理想化清单 #9）。错误时读返回全 1、写丢弃，
 * 记录到 StatusUnit，仍以成功完成结束。
 *
 * @see spec-pcie
 */

#ifndef __GW100_PCIE_BAR2_BAR2_HANDLER_HH__
#define __GW100_PCIE_BAR2_BAR2_HANDLER_HH__

#include "base/types.hh"
#include "src/gw100/pcie/bar2/devmem_backend.hh"
#include "src/gw100/pcie/pcie_types.hh"
#include "src/gw100/pcie/status/status_unit.hh"
#include "src/gw100/pcie/window/window_table.hh"

namespace gem5
{
namespace gw100
{

/** BAR2 的处理者（spec-pcie §7） */
class Bar2Handler : public BarHandler
{
  public:
    /**
     * 构造；窗口来自 table，设备内存经 devmem 访问，错误记录到 status，
     * 每次访问的延迟为 latency
     */
    Bar2Handler(WindowTable &table, StatusUnit &status,
                DevmemBackend &devmem, Tick latency)
        : table(table), status(status), devmem(devmem), latency(latency)
    {}

    /**
     * 处理一次访问：按窗口换算成 DPA，检查使能、目标范围和只读后经
     * DevmemBackend 访问设备内存
     */
    Tick access(BarAccess &a) override;

  private:
    /** 保存窗口配置的表 */
    WindowTable &table;
    /** 记录错误的状态单元 */
    StatusUnit &status;
    /** 访问设备内存的后端 */
    DevmemBackend &devmem;
    /** 每次访问的设备侧延迟 */
    Tick latency;
};

} // namespace gw100
} // namespace gem5

#endif // __GW100_PCIE_BAR2_BAR2_HANDLER_HH__
