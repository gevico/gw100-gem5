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

/** 启动与设备状态块的起始（spec-pcie §6.5） */
constexpr Addr BootBlk = 0x1'0000;
/** ML 的 HIF 侧错误寄存器块的起始 */
constexpr Addr MlBlk = 0x1'3000;

} // anonymous namespace

/** 构造：boot 为 BAR0 的启动寄存器块 */
HifRegs::HifRegs(BootRegs &boot) : boot(boot)
{
}

/** 读偏移 off 的 32 位寄存器；未实现时返回 false */
bool
HifRegs::read(Addr off, uint32_t &v) const
{
    switch (off) {
      case BootBlk + 0x08: v = boot.fwVersion(); return true;
      case BootBlk + 0x10: v = pciState ? pciState() : 0; return true;
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
    switch (off) {
      case BootBlk + 0x08: boot.setFwVersion(v); return true;
      case BootBlk + 0x10: return true;  // PCI_STATE 只读
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
