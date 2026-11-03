/**
 * @file  mgmt_mailbox.cc
 * @brief MgmtMailbox 的实现
 *
 * @see spec-mgmt-tile
 */

#include "src/gw100/mgmt/mailbox/mgmt_mailbox.hh"

#include "base/logging.hh"

namespace gem5
{
namespace gw100
{

namespace
{

/** 有效槽位的掩码 */
uint64_t
slotMask(unsigned slots)
{
    return slots >= 64 ? ~0ULL : ((1ULL << slots) - 1);
}

} // anonymous namespace

/** 构造：slots 为 doorbell 槽位数（mgmt.db_slots，不超过 64） */
MgmtMailbox::MgmtMailbox(unsigned slots) : slots(slots), slot(slots, 0)
{
    fatal_if(slots == 0 || slots > 64, "doorbell 槽位数 %d 超出布局", slots);
}

/** ML 的 DB_WR：覆盖槽位 slot 并置位 pending；越界返回 false */
bool
MgmtMailbox::dbWrite(unsigned s, uint16_t value)
{
    if (s >= slots)
        return false;
    slot[s] = value;
    dbPending |= 1ULL << s;
    if (onChange)
        onChange();
    return true;
}

/** ML 的 EVENT/BARRIER 或 Tile 事件：按位或入 EVT_PENDING */
void
MgmtMailbox::eventSet(uint32_t mask)
{
    evtPending |= mask;
    if (onChange)
        onChange();
}

/** PLIC 源 1 的电平：DB_PENDING & ~DB_MASK 非零 */
bool
MgmtMailbox::dbIrq() const
{
    return (dbPending & ~dbMask & slotMask(slots)) != 0;
}

/**
 * CPU 读区内偏移 off 的 32 位寄存器；未实现的偏移或宽度不是 4
 * 时返回 false
 */
bool
MgmtMailbox::read(Addr off, unsigned size, uint32_t &v) const
{
    if (size != 4 || off % 4)
        return false;
    if (off < 4ULL * slots) {
        v = slot[off / 4];
        return true;
    }
    switch (off) {
      case 0x100: v = dbPending & 0xFFFFFFFF; return true;
      case 0x104: v = (dbPending >> 32) & 0xFFFFFFFF; return true;
      case 0x108: v = dbMask & 0xFFFFFFFF; return true;
      case 0x10C: v = (dbMask >> 32) & 0xFFFFFFFF; return true;
      case 0x200: v = evtPending; return true;
      case 0x204: v = evtMask; return true;
    }
    return false;
}

/** CPU 写区内偏移 off 的 32 位寄存器；规则同 read() */
bool
MgmtMailbox::write(Addr off, unsigned size, uint32_t v)
{
    if (size != 4 || off % 4)
        return false;
    uint64_t valid = slotMask(slots);
    if (off < 4ULL * slots)
        return true;  // DB_SLOT 只读，写入被忽略
    switch (off) {
      case 0x100: dbPending &= ~(uint64_t(v) & valid); break;
      case 0x104: dbPending &= ~((uint64_t(v) << 32) & valid); break;
      case 0x108:
        dbMask = (dbMask & ~0xFFFFFFFFULL) | (v & valid);
        break;
      case 0x10C:
        dbMask = (dbMask & 0xFFFFFFFFULL) |
                 ((uint64_t(v) << 32) & valid);
        break;
      case 0x200: evtPending &= ~v; break;
      case 0x204: evtMask = v; break;
      default: return false;
    }
    if (onChange)
        onChange();
    return true;
}

} // namespace gw100
} // namespace gem5
