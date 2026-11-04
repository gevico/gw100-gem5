/**
 * @file  hif_regs.cc
 * @brief HifRegs 的实现
 *
 * @see spec-pcie, spec-mgmt-link
 */

#include "src/gw100/pcie/hif/hif_regs.hh"

#include <cstring>

namespace gem5
{
namespace gw100
{

namespace
{

/** HDMA 寄存器区的大小（HDMA_BASE = 0，spec-host-dma §7.1） */
constexpr Addr HdmaSize = 0x1000;
/** 启动与设备状态块的起始（spec-pcie §6.5） */
constexpr Addr BootBlk = 0x1'0000;
/** 中断请求与 FLR 块的起始 */
constexpr Addr IntBlk = 0x1'2000;
/** CTX_VECTOR[0] 的偏移 */
constexpr Addr CtxVecBase = IntBlk + 0x100;
/** ML 的 HIF 侧错误寄存器块的起始 */
constexpr Addr MlBlk = 0x1'3000;

} // anonymous namespace

/** 构造：boot 为 BAR0 的启动寄存器块，ctrl 为启动与 FLR 控制 */
HifRegs::HifRegs(BootRegs &boot, BootCtrl &ctrl) : boot(boot), ctrl(ctrl)
{
}

/** 读偏移 off 的 32 位寄存器；未实现时返回 false */
bool
HifRegs::read(Addr off, uint32_t &v) const
{
    if (off < HdmaSize)
        return hdma && hdma->read(off, v, false);
    if (intCtrl && off >= CtxVecBase &&
        off < CtxVecBase + 4 * intCtrl->contexts() && off % 4 == 0) {
        v = intCtrl->ctxVector((off - CtxVecBase) / 4);
        return true;
    }
    switch (off) {
      case BootBlk + 0x00: v = ctrl.fwBootStatus(); return true;
      case BootBlk + 0x04: v = ctrl.fwSize(); return true;
      case BootBlk + 0x08: v = boot.fwVersion(); return true;
      case BootBlk + 0x0C:
        v = (ctrl.debugWin ? 1u : 0) | (ctrl.devFatal() ? 2u : 0);
        return true;
      case BootBlk + 0x10: v = pciState ? pciState() : 0; return true;
      case BootBlk + 0x14: v = ctrl.bootLoadStatus(); return true;
      case IntBlk + 0x00: v = 0; return true;  // INT_REQ 是 W1S
      case IntBlk + 0x04:
        v = intCtrl ? intCtrl->levelReg() : 0;
        return true;
      case IntBlk + 0x10: v = 0; return true;  // FLR_CTRL 是 W1S
      case IntBlk + 0x14: v = ctrl.flrStatus(); return true;
      case MlBlk + 0x00: v = ml ? ml->errReg() : 0; return true;
      case MlBlk + 0x04: v = ml ? ml->errInfoReg() : 0; return true;
      case MlBlk + 0x08: v = ml ? ml->errAddrReg() : 0; return true;
    }
    return false;
}

/** 写偏移 off 的 32 位寄存器；未实现时返回 false */
bool
HifRegs::write(Addr off, uint32_t v)
{
    if (off < HdmaSize)
        return hdma && hdma->write(off, v);
    if (intCtrl && off >= CtxVecBase &&
        off < CtxVecBase + 4 * intCtrl->contexts() && off % 4 == 0) {
        intCtrl->setCtxVector((off - CtxVecBase) / 4, v);
        return true;
    }
    switch (off) {
      case BootBlk + 0x00: ctrl.fwWriteBootStatus(v); return true;
      case BootBlk + 0x04: ctrl.fwWriteFwSize(v); return true;
      case BootBlk + 0x08: boot.setFwVersion(v); return true;
      case BootBlk + 0x0C:
        // DEBUG_WIN 由固件写入（熔丝禁止开启不建模，视为允许）；
        // DEV_FATAL 只能置位
        ctrl.debugWin = v & 1;
        if (v & 2)
            ctrl.setDevFatal();
        return true;
      case BootBlk + 0x10:
      case BootBlk + 0x14:
        return true;  // 只读
      case IntBlk + 0x00:
        if (intCtrl)
            intCtrl->request(v);
        return true;
      case IntBlk + 0x04:
        if (intCtrl)
            intCtrl->setLevel(v);
        return true;
      case IntBlk + 0x10: ctrl.flrCtrl(v); return true;
      case IntBlk + 0x14: return true;  // 只读
      case MlBlk + 0x00:
        if (ml)
            ml->clearErr(v);
        return true;
      case MlBlk + 0x04:
      case MlBlk + 0x08:
        return true;  // 只读
    }
    return false;
}

/** 处理管理 CPU 经 ML 发来的 REG_RD、REG_WR */
void
HifRegs::mlRequest(const MlTxnPtr &txn, MlRespond respond)
{
    MlData d{};
    if (txn->len != 4 || txn->addr % 4) {
        respond(MlStatus::Reject, d);
        return;
    }
    bool ok = false;
    if (txn->type == MlType::RegRd) {
        uint32_t v = 0;
        ok = read(txn->addr, v);
        std::memcpy(d.data(), &v, 4);
    } else if (txn->type == MlType::RegWr) {
        uint32_t v;
        std::memcpy(&v, txn->data.data(), 4);
        ok = write(txn->addr, v);
    }
    respond(ok ? MlStatus::Ok : MlStatus::Reject, d);
}

} // namespace gw100
} // namespace gem5
