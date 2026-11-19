/**
 * @file ct_windows.hh
 * @brief compute tile 内部的窗口访问构建，可考虑和外部框架放在一起
 *        单独写出的原因是为了更清晰，将外部的地址量转化成构建的内部偏移
 *        并送往对应构建
 */

#ifndef __GW100_CT_WINDOWS_CT_WINDOWS_HH__
#define __GW100_CT_WINDOWS_CT_WINDOWS_HH__

#include <cstring>
#include <vector>

#include "src/gw100/ct/ct_if.hh"
#include "src/gw100/ct/ct_types.hh"
#include "src/gw100/ct/ctrl/ct_ctrl_ram.hh"

namespace gem5
{
namespace gw100
{
class CtWindow : public CtWindowHandler
{
  public:
    /** 记录 ct 内部自构建具体的信息，用于地址匹配调用 */
    struct SubWindow
    {
        Addr base;
        Addr size;
        CtWindowHandler *handler;
    };

    /** 注册自构建的信息 */
    CtWindow(CtWindowHandler &sram, CtWindowHandler &sync, CtCtrlSram &ctrl_ram, Addr base = 0);

    ~CtWindow() override = default;

    /** 根据 subwindows 进行具体的下级构建调度 */
    CtAccessResult access(CtAccess &a, Tick now) override;
    void reset() override;

  private:
    /** 实际进行信息储存的 vector */
    std::vector<SubWindow> SubWindows;
    /** 访问 tile 的地址偏移，0 默认全局 */
    Addr windowsBase;
    /** 进行地址匹配和返回特定状态，检验合法并算出单个 tile 内部的偏移 */
    CtStatus normalizeAddr(Addr base, unsigned size, Addr &right_off) const;
};

} 
} 

#endif
