/**
 * @file  hif_regs.hh
 * @brief 固件可见的 HIF 寄存器区：ML 的 HIF 侧目标
 *
 * 管理 CPU 经 ML 的 REG_RD、REG_WR 访问本区（spec-pcie §6.5），偏移
 * 相对于本地地址 0x3000_0000。只接受 32 位访问；未分配或尚未实现的
 * 偏移，读以 REJECT 结束，写被拒绝并由 ML 记录 WR_REJECT。
 *
 * @see spec-pcie, spec-mgmt-link
 */

#ifndef __GW100_PCIE_HIF_HIF_REGS_HH__
#define __GW100_PCIE_HIF_HIF_REGS_HH__

#include <cstdint>
#include <functional>

#include "src/gw100/ml/ml_endpoint.hh"
#include "src/gw100/pcie/bar0/boot_regs.hh"
#include "src/gw100/pcie/boot/boot_ctrl.hh"

namespace gem5
{
namespace gw100
{

/** 固件可见的 HIF 寄存器区 */
class HifRegs : public MlTargetIf
{
  public:
    /** 构造：boot 为 BAR0 的启动寄存器块，ctrl 为启动与 FLR 控制 */
    HifRegs(BootRegs &boot, BootCtrl &ctrl);

    /** 接入 ML 的 HIF 侧适配器，ML 错误寄存器经它读写 */
    void bindMl(MlEndpoint *ep) { ml = ep; }

    /** 读取配置空间中固件需要的状态（PCI_STATE）的回调 */
    std::function<uint32_t()> pciState;

    /** 处理管理 CPU 经 ML 发来的 REG_RD、REG_WR */
    void mlRequest(const MlTxnPtr &txn, MlRespond respond) override;

  private:
    /** 读偏移 off 的 32 位寄存器；未实现时返回 false */
    bool read(Addr off, uint32_t &v) const;

    /** 写偏移 off 的 32 位寄存器；未实现时返回 false */
    bool write(Addr off, uint32_t v);

    /** BAR0 的启动寄存器块 */
    BootRegs &boot;
    /** 启动与 FLR 控制 */
    BootCtrl &ctrl;
    /** ML 的 HIF 侧适配器 */
    MlEndpoint *ml = nullptr;
};

} // namespace gw100
} // namespace gem5

#endif // __GW100_PCIE_HIF_HIF_REGS_HH__
