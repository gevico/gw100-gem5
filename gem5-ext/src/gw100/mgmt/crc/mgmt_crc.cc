/**
 * @file  mgmt_crc.cc
 * @brief MgmtCrc 的实现
 *
 * @see spec-clock-reset-power
 */

#include "src/gw100/mgmt/crc/mgmt_crc.hh"

namespace gem5
{
namespace gw100
{

/** 构造；now 为复位撤销的时刻，PLL_TS 从此时开始锁定 */
MgmtCrc::MgmtCrc(const Config &cfg, Tick now)
    : cfg(cfg), tsLockAt(now + cfg.pllLock)
{
}

/** 读偏移 off 的 32 位寄存器；未实现的偏移返回 false */
bool
MgmtCrc::read(Addr off, uint32_t &v, Tick now) const
{
    switch (off) {
      case 0x000: v = resetCause; return true;
      case 0x010: v = meshRst; return true;
      case 0x014: v = ctRst; return true;
      case 0x018: v = ctClkEn; return true;
      case 0x01C: v = ctIso; return true;
      // NI 隔离的应答与失败随 NoC 和 Compute Tile 的模型实现
      case 0x020: v = 0; return true;
      case 0x024: v = 0; return true;
      case 0x030: v = pllCoreCfg; return true;
      case 0x034: v = pllDdrCfg; return true;
      case 0x038:
        v = (now >= coreLockAt ? 1u : 0) | (now >= ddrLockAt ? 2u : 0) |
            (now >= tsLockAt ? 4u : 0);
        return true;
      case 0x03C:
        v = now >= switchDoneAt ? clkSelNext : clkSel;
        return true;
    }
    return false;
}

/**
 * 写偏移 off 的 32 位寄存器，返回完成时刻；未实现的偏移返回
 * MaxTick。写 CLK_SEL 引起切换时，完成时刻在切换结束之后
 */
Tick
MgmtCrc::write(Addr off, uint32_t v, Tick now)
{
    Tick done = now + cfg.access;
    switch (off) {
      case 0x000: resetCause &= ~v; break;
      case 0x010: meshRst &= v | ~0x1Fu; break;  // 只能由 1 写为 0
      case 0x014: ctRst = v & 0xFFFF; break;
      case 0x018: ctClkEn = v & 0xFFFF; break;
      case 0x01C: ctIso = v & 0xFFFF; break;
      case 0x020:
      case 0x024:
      case 0x038:
        break;  // 只读
      case 0x030:
        if ((v & 1) && !(pllCoreCfg & 1))
            coreLockAt = done + cfg.pllLock;
        else if (!(v & 1))
            coreLockAt = MaxTick;
        pllCoreCfg = v;
        break;
      case 0x034:
        if ((v & 1) && !(pllDdrCfg & 1))
            ddrLockAt = done + cfg.pllLock;
        else if (!(v & 1))
            ddrLockAt = MaxTick;
        pllDdrCfg = v;
        break;
      case 0x03C: {
        if (now >= switchDoneAt)
            clkSel = clkSelNext;
        // 只能由 XTAL 切到已锁定的 PLL_CORE；不允许切回（§3.4）
        uint32_t want = clkSel | (v & 3);
        if (done < coreLockAt)
            want = clkSel;
        if (want != clkSel) {
            switchDoneAt = done + cfg.switchPause;
            clkSelNext = want;
            if ((want & 2) && !(clkSel & 2) && onMgmtSwitch)
                onMgmtSwitch(switchDoneAt);
            done = switchDoneAt;
        }
        break;
      }
      default:
        return MaxTick;
    }
    return done;
}

} // namespace gw100
} // namespace gem5
