/**
 * @file  mgmt_sram.hh
 * @brief 管理 Tile 的 SRAM 存储、bank 仲裁与复位清零
 *
 * SRAM 由若干个连续的单口 bank 组成（spec-mgmt-tile §4.2）。每次 64 位
 * 访问占用所在 bank 一个周期，不足 64 位的写做 RMW，计两次访问
 * （§4.3）；同一 bank 上的访问按到达顺序排队，因此每个 initiator 的
 * 等待时间有上限。复位后 SRAM 由硬件清零，每个 bank 每周期写一个
 * 64 位字，完成后置 INIT_DONE。ROM 是单独的宏单元，不与 SRAM 竞争。
 *
 * @see spec-mgmt-tile
 */

#ifndef __GW100_MGMT_SRAM_MGMT_SRAM_HH__
#define __GW100_MGMT_SRAM_MGMT_SRAM_HH__

#include <cstdint>
#include <vector>

#include "base/types.hh"

namespace gem5
{
namespace gw100
{

/** 管理 Tile 的 SRAM 存储与参考调度 */
class MgmtSram
{
  public:
    /** 本地地址：SRAM 的起始 */
    static constexpr Addr SramBase = 0x1000'0000;

    /** 构造：sram_size 为字节数，banks 为 SRAM 的 bank 数 */
    MgmtSram(uint64_t sram_size, unsigned banks);

    /** [a, a + size) 是否整个落在 SRAM 内 */
    bool inSram(Addr a, unsigned size) const;

    /** 本地地址 a 处的存储；调用者已用 inSram 检查范围 */
    uint8_t *ptr(Addr a);

    /**
     * 在 now 时刻为一次 SRAM 访问排队，返回完成时刻。period 为
     * clk_mgmt 的周期；访问 [a, a + size) 不跨 bank
     */
    Tick schedule(Addr a, unsigned size, bool write, Tick now,
                  Tick period);

    /** 复位：开始硬件清零，记录完成时刻 */
    void startInit(Tick now, Tick period);

    /** 时刻 now 时清零是否已完成（SRAM_STATUS.INIT_DONE） */
    bool initDone(Tick now) const { return now >= initDoneAt; }

    /** 清零完成的时刻 */
    Tick initDoneTick() const { return initDoneAt; }

    /** SRAM 的字节数 */
    const uint64_t sramSize;

  private:
    /** 本地地址 a 所在的 bank */
    unsigned bankOf(Addr a) const;

    /** bank 数 */
    const unsigned banks;
    /** SRAM 的内容 */
    std::vector<uint8_t> sram;
    /** 各 bank 空闲的时刻 */
    std::vector<Tick> bankFree;
    /** 清零完成的时刻 */
    Tick initDoneAt = MaxTick;
};

} // namespace gw100
} // namespace gem5

#endif // __GW100_MGMT_SRAM_MGMT_SRAM_HH__
