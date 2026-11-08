/**
 * @file  mgmt_sram.cc
 * @brief MgmtSram 的实现
 *
 * @see spec-mgmt-tile
 */

#include "src/gw100/mgmt/sram/mgmt_sram.hh"

#include <algorithm>

#include "base/logging.hh"

namespace gem5
{
namespace gw100
{

/** 构造：sram_size 为字节数，banks 为 SRAM 的 bank 数 */
MgmtSram::MgmtSram(uint64_t sram_size, unsigned banks)
    : sramSize(sram_size), banks(banks),
      sram(sram_size, 0), bankFree(banks, 0)
{
    fatal_if(banks == 0 || sram_size % banks != 0,
             "SRAM 容量 %d 不能均分为 %d 个 bank", sram_size, banks);
}

/** [a, a + size) 是否整个落在 SRAM 内 */
bool
MgmtSram::inSram(Addr a, unsigned size) const
{
    return a >= SramBase && a + size <= SramBase + sramSize;
}

/** 本地地址 a 处的存储；调用者已用 inSram 检查范围 */
uint8_t *
MgmtSram::ptr(Addr a)
{
    return &sram[a - SramBase];
}

/** 本地地址 a 所在的 bank */
unsigned
MgmtSram::bankOf(Addr a) const
{
    return (a - SramBase) / (sramSize / banks);
}

/**
 * 在 now 时刻为一次 SRAM 访问排队，返回完成时刻。period 为
 * clk_mgmt 的周期；访问 [a, a + size) 不跨 bank
 */
Tick
MgmtSram::schedule(Addr a, unsigned size, bool write, Tick now,
                   Tick period)
{
    // 访问涉及的 64 位字；写入不足一个字的部分要先读出（RMW）
    Addr first = a / 8;
    Addr last = (a + size - 1) / 8;
    unsigned accesses = last - first + 1;
    if (write) {
        bool first_partial =
            a % 8 != 0 || (first == last && (a + size) % 8 != 0);
        bool last_partial = last != first && (a + size) % 8 != 0;
        accesses += (first_partial ? 1 : 0) + (last_partial ? 1 : 0);
    }
    unsigned b = bankOf(a);
    Tick start = std::max(now, bankFree[b]);
    bankFree[b] = start + accesses * period;
    return bankFree[b];
}

/** 复位：开始硬件清零，记录完成时刻 */
void
MgmtSram::startInit(Tick now, Tick period)
{
    std::fill(sram.begin(), sram.end(), 0);
    // 每个 bank 每周期写一个 64 位字，各 bank 并行
    uint64_t words_per_bank = sramSize / banks / 8;
    initDoneAt = now + words_per_bank * period;
    std::fill(bankFree.begin(), bankFree.end(), initDoneAt);
}

} // namespace gw100
} // namespace gem5
