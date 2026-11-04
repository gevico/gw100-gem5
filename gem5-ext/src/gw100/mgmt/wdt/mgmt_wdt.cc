/**
 * @file  mgmt_wdt.cc
 * @brief MgmtWdt 的实现
 *
 * @see spec-mgmt-tile
 */

#include "src/gw100/mgmt/wdt/mgmt_wdt.hh"

namespace gem5
{
namespace gw100
{

/** 读偏移 off 处 size 字节的寄存器；宽度不符或未实现时返回 false */
bool
MgmtWdt::read(Addr off, unsigned size, uint64_t &v) const
{
    switch (off) {
      case 0x00: v = ctrl; return size == 4;
      case 0x08: v = load; return size == 8;
      case 0x10: v = 0; return size == 4;       // WDT_KICK 读为 0
      case 0x14: v = status; return size == 4;
    }
    return false;
}

/**
 * 在样本 s 所在的周期写偏移 off 处 size 字节的寄存器，再按写入后的
 * 状态判定到期；宽度不符或未实现时返回 false
 */
bool
MgmtWdt::write(Addr off, unsigned size, uint64_t v, uint64_t s)
{
    uint64_t dummy;
    if (!read(off, size, dummy))
        return false;
    if (status & Stage2)
        return true;  // 第二级之后写入一律无效
    bool locked = ctrl & CtrlLock;  // 按写入之前的 LOCK 判定
    uint32_t v32 = static_cast<uint32_t>(v);
    switch (off) {
      case 0x00:
        if (locked)
            break;
        if ((v32 & CtrlEn) && !(ctrl & CtrlEn))
            t0 = s;
        if (!(v32 & CtrlEn) && (ctrl & CtrlEn))
            status &= ~Stage1;
        ctrl = v32 & (CtrlEn | CtrlLock);
        break;
      case 0x08:
        if (locked)
            break;
        load = v;
        t0 = s;
        break;
      case 0x10:
        if (!(ctrl & CtrlEn))
            break;  // EN = 0 时喂狗不改变任何状态
        if (v32 == KickMagic) {
            t0 = s;
            status &= ~Stage1;
        } else {
            expire(s);
        }
        break;
      default:
        break;  // WDT_STATUS 只读
    }
    evaluate(s);
    return true;
}

/** 在样本 s 所在的周期判定一次自然到期 */
void
MgmtWdt::evaluate(uint64_t s)
{
    if (counting() && s - t0 >= period())
        expire(s);
}

/** 一次到期（自然到期或非法喂狗）：推进一级，T0 ← s */
void
MgmtWdt::expire(uint64_t s)
{
    t0 = s;
    if (status & Stage1)
        status |= Stage2;
    else
        status |= Stage1;
}

} // namespace gw100
} // namespace gem5
