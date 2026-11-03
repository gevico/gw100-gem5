/**
 * @file  mgmt_local_regs.cc
 * @brief MgmtLocalRegs 的实现
 *
 * @see spec-mgmt-tile, spec-mgmt-link
 */

#include "src/gw100/mgmt/regs/mgmt_local_regs.hh"

namespace gem5
{
namespace gw100
{

namespace
{

/** 本地地址：看门狗 */
constexpr Addr WdtBase = 0x2000'C000;
/** 本地地址：ML 寄存器（ML_BASE） */
constexpr Addr MlBase = 0x2000'D000;
/** 本地地址：SRAM 控制 */
constexpr Addr SramCtlBase = 0x2000'E000;
/** 本地地址：访问控制 */
constexpr Addr AclBase = 0x2000'F000;

} // anonymous namespace

/** 构造：sram 提供 INIT_DONE */
MgmtLocalRegs::MgmtLocalRegs(MgmtSram &sram) : sram(sram)
{
}

/**
 * CPU 读本地地址 a 处 size 字节；未实现的寄存器或宽度不符时返回
 * false（访问错误异常）
 */
bool
MgmtLocalRegs::read(Addr a, unsigned size, uint64_t &v, Tick now) const
{
    // 32 位寄存器
    if (size == 4) {
        switch (a) {
          case WdtBase + 0x00: v = wdtCtrlReg; return true;
          case WdtBase + 0x10: v = 0; return true;  // WDT_KICK 只写
          case WdtBase + 0x14: v = wdtStatus; return true;
          case MlBase + 0x00: v = ml ? ml->statusReg() : 0; return true;
          case MlBase + 0x04: v = ml ? ml->errReg() : 0; return true;
          case MlBase + 0x08: v = ml ? ml->errInfoReg() : 0; return true;
          case MlBase + 0x0C: v = ml ? ml->errAddrReg() : 0; return true;
          case SramCtlBase + 0x00: v = sram.initDone(now) ? 1 : 0;
            return true;
          case SramCtlBase + 0x04: v = eccStatus; return true;
          case SramCtlBase + 0x08: v = ceCount; return true;
          case SramCtlBase + 0x0C: v = ceThresh; return true;
          case AclBase + 0x00: v = aclLock ? 1 : 0; return true;
          case AclBase + 0xC0: v = aclViol; return true;
        }
        for (unsigned k = 0; k < NocWindows; k++) {
            if (a == AclBase + 0x50 + 0x20 * k) {
                v = nocWin[k].attr;
                return true;
            }
        }
        return false;
    }
    // 64 位寄存器
    if (size == 8) {
        switch (a) {
          case WdtBase + 0x08: v = wdtLoad; return true;
          case SramCtlBase + 0x10: v = ueAddr; return true;
          case AclBase + 0x08: v = descBase; return true;
          case AclBase + 0x10: v = descLimit; return true;
          case AclBase + 0xC8: v = aclViolAddr; return true;
        }
        for (unsigned k = 0; k < NocWindows; k++) {
            if (a == AclBase + 0x40 + 0x20 * k) {
                v = nocWin[k].base;
                return true;
            }
            if (a == AclBase + 0x48 + 0x20 * k) {
                v = nocWin[k].limit;
                return true;
            }
        }
    }
    return false;
}

/** CPU 写本地地址 a 处 size 字节；规则同 read() */
bool
MgmtLocalRegs::write(Addr a, unsigned size, uint64_t v)
{
    uint64_t dummy;
    if (!read(a, size, dummy, 0))
        return false;
    uint32_t v32 = static_cast<uint32_t>(v);
    switch (a) {
      case WdtBase + 0x00:
        if (wdtCtrlReg & 2) {
            // 锁定后 EN 不能再修改，LOCK 保持
            break;
        }
        wdtCtrlReg = v32 & 3;
        break;
      case WdtBase + 0x08:
        if (!(wdtCtrlReg & 2))
            wdtLoad = v;
        break;
      case MlBase + 0x04:
        if (ml)
            ml->clearErr(v32);
        break;
      case SramCtlBase + 0x04: eccStatus &= ~v32; break;
      case SramCtlBase + 0x08: ceCount = 0; break;
      case SramCtlBase + 0x0C: ceThresh = v32; break;
      case AclBase + 0xC0: aclViol &= ~v32; break;
      default:
        // 访问控制的配置：锁定后写入被忽略
        if (a >= AclBase && a < AclBase + 0xC0 && !aclLock) {
            if (a == AclBase + 0x00) {
                aclLock = v32 & 1;
            } else if (a == AclBase + 0x08) {
                descBase = v & ~7ULL;
            } else if (a == AclBase + 0x10) {
                descLimit = v & ~7ULL;
            } else {
                unsigned k = (a - AclBase - 0x40) / 0x20;
                Addr o = (a - AclBase - 0x40) % 0x20;
                if (o == 0x00)
                    nocWin[k].base = v & ~7ULL;
                else if (o == 0x08)
                    nocWin[k].limit = v & ~7ULL;
                else
                    nocWin[k].attr = v32 & 3;
            }
        }
        break;
    }
    if (onChange)
        onChange();
    return true;
}

/** HDMA 经 ML 访问 [a, a + len) 是否落在 DESC_BASE – DESC_LIMIT 内 */
bool
MgmtLocalRegs::descAllowed(Addr a, unsigned len) const
{
    return descBase < descLimit && a >= descBase && a + len <= descLimit;
}

/** 记录一次访问控制违例（bit 为 ViolBoot 或 ViolNoc） */
void
MgmtLocalRegs::recordViol(unsigned bit, Addr a)
{
    if (aclViol == 0)
        aclViolAddr = a;
    aclViol |= 1u << bit;
    if (onChange)
        onChange();
}

/** PLIC 源 3 的电平：ML_ERR 非零 */
bool
MgmtLocalRegs::mlIrq() const
{
    return ml && ml->errReg() != 0;
}

/** PLIC 源 4 的电平：THRESH ≠ 0 且 COUNT ≥ THRESH */
bool
MgmtLocalRegs::ceIrq() const
{
    return ceThresh != 0 && ceCount >= ceThresh;
}

} // namespace gw100
} // namespace gem5
