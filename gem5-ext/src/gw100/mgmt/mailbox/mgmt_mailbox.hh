/**
 * @file  mgmt_mailbox.hh
 * @brief 管理 Tile 的 doorbell 信箱与事件寄存器
 *
 * doorbell 写入覆盖槽位并置位 pending，不排队；事件只置位、不排队
 * （spec-mgmt-tile §6.2、§6.3）。寄存器偏移见 §3.5，相对于
 * 0x2001_0000。pending 与 mask 决定 PLIC 源 1、2 的电平。
 *
 * @see spec-mgmt-tile
 */

#ifndef __GW100_MGMT_MAILBOX_MGMT_MAILBOX_HH__
#define __GW100_MGMT_MAILBOX_MGMT_MAILBOX_HH__

#include <cstdint>
#include <functional>
#include <vector>

#include "base/types.hh"

namespace gem5
{
namespace gw100
{

/** doorbell 信箱与事件寄存器 */
class MgmtMailbox
{
  public:
    /** 本地地址：本区的起始 */
    static constexpr Addr Base = 0x2001'0000;
    /** 本区的大小 */
    static constexpr Addr Size = 0x1'0000;

    /** 构造：slots 为 doorbell 槽位数（mgmt.db_slots，不超过 64） */
    explicit MgmtMailbox(unsigned slots);

    /** ML 的 DB_WR：覆盖槽位 slot 并置位 pending；越界返回 false */
    bool dbWrite(unsigned slot, uint16_t value);

    /** ML 的 EVENT/BARRIER 或 Tile 事件：按位或入 EVT_PENDING */
    void eventSet(uint32_t mask);

    /**
     * CPU 读区内偏移 off 的 32 位寄存器；未实现的偏移或宽度不是 4
     * 时返回 false
     */
    bool read(Addr off, unsigned size, uint32_t &v) const;

    /** CPU 写区内偏移 off 的 32 位寄存器；规则同 read() */
    bool write(Addr off, unsigned size, uint32_t v);

    /** PLIC 源 1 的电平：DB_PENDING & ~DB_MASK 非零 */
    bool dbIrq() const;

    /** PLIC 源 2 的电平：EVT_PENDING & ~EVT_MASK 非零 */
    bool evtIrq() const { return (evtPending & ~evtMask) != 0; }

    /** 中断电平可能变化时调用 */
    std::function<void()> onChange;

  private:
    /** 槽位数 */
    const unsigned slots;
    /** DB_SLOT[i] */
    std::vector<uint16_t> slot;
    /** DB_PENDING，位 i 对应槽位 i */
    uint64_t dbPending = 0;
    /** DB_MASK */
    uint64_t dbMask = 0;
    /** EVT_PENDING */
    uint32_t evtPending = 0;
    /** EVT_MASK */
    uint32_t evtMask = 0;
};

} // namespace gw100
} // namespace gem5

#endif // __GW100_MGMT_MAILBOX_MGMT_MAILBOX_HH__
