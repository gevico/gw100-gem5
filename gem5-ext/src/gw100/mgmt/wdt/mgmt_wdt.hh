/**
 * @file  mgmt_wdt.hh
 * @brief 管理 Tile 的看门狗：两级超时的状态机
 *
 * 位于本地地址 0x2000_C000（spec-mgmt-tile §3.5、§7.4）。时间以同步到
 * clk_mgmt 域的 mtime 样本表示：调用者在每次访问和每个可能到期的时钟
 * 沿给出当时的样本 S，本类只做状态转移，不关心 tick 和时钟。到期判定
 * 为 (S − T0) mod 2^64 ≥ max(WDT_LOAD, 1)，到期后 T0 ← S，每次判定
 * 最多到期一次。同一周期先处理写入，再判定到期。
 *
 * @see spec-mgmt-tile
 */

#ifndef __GW100_MGMT_WDT_MGMT_WDT_HH__
#define __GW100_MGMT_WDT_MGMT_WDT_HH__

#include <cstdint>

#include "base/types.hh"

namespace gem5
{
namespace gw100
{

/** 看门狗的寄存器与状态机 */
class MgmtWdt
{
  public:
    /** 本地地址：看门狗寄存器的起始 */
    static constexpr Addr Base = 0x2000'C000;
    /** 看门狗寄存器区的大小 */
    static constexpr Addr Size = 0x1000;
    /** 合法喂狗写入 WDT_KICK 的值 */
    static constexpr uint32_t KickMagic = 0x5A5A'A5A5;

    /** 读偏移 off 处 size 字节的寄存器；宽度不符或未实现时返回 false */
    bool read(Addr off, unsigned size, uint64_t &v) const;

    /**
     * 在样本 s 所在的周期写偏移 off 处 size 字节的寄存器，再按写入后的
     * 状态判定到期；宽度不符或未实现时返回 false
     */
    bool write(Addr off, unsigned size, uint64_t v, uint64_t s);

    /** 在样本 s 所在的周期判定一次自然到期 */
    void evaluate(uint64_t s);

    /** 是否正在计时：EN = 1 且 STAGE2 = 0 */
    bool counting() const { return (ctrl & CtrlEn) && !(status & Stage2); }

    /** 下一次自然到期时的样本值 T0 + L（只在 counting() 时有意义） */
    uint64_t deadline() const { return t0 + period(); }

    /** PLIC 源 7 的电平：STAGE1 */
    bool stage1() const { return status & Stage1; }

    /** 第二级是否已经超时 */
    bool stage2() const { return status & Stage2; }

  private:
    /** WDT_CTRL.EN */
    static constexpr uint32_t CtrlEn = 1u << 0;
    /** WDT_CTRL.LOCK */
    static constexpr uint32_t CtrlLock = 1u << 1;
    /** WDT_STATUS.STAGE1 */
    static constexpr uint32_t Stage1 = 1u << 0;
    /** WDT_STATUS.STAGE2 */
    static constexpr uint32_t Stage2 = 1u << 1;

    /** 到期周期 L = max(WDT_LOAD, 1) */
    uint64_t period() const { return load ? load : 1; }

    /** 一次到期（自然到期或非法喂狗）：推进一级，T0 ← s */
    void expire(uint64_t s);

    /** WDT_CTRL：[0] EN，[1] LOCK */
    uint32_t ctrl = 0;
    /** WDT_LOAD */
    uint64_t load = 0;
    /** WDT_STATUS：[0] STAGE1，[1] STAGE2 */
    uint32_t status = 0;
    /** 基准时刻 T0（mtime 样本） */
    uint64_t t0 = 0;
};

} // namespace gw100
} // namespace gem5

#endif // __GW100_MGMT_WDT_MGMT_WDT_HH__
