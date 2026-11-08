/**
 * @file  ecc_status.hh
 * @brief 管理 SRAM 的 CE/UE 状态及同 tick 优先级
 * @see spec-mgmt-tile, spec-sram
 */
#pragma once
#include <algorithm>
#include <cstdint>
#include <limits>

namespace gem5::gw100
{
/** 保持硬件置位优先的管理 SRAM 软件可见 ECC 状态。 */
struct MgmtEccStatus
{
    /** 粘滞 CE/UE 位、饱和 CE 计数和中断阈值。 */
    uint32_t status = 0, count = 0, threshold = 0;
    /** 首次 UE 的本地码字地址。 */
    uint64_t address = 0;
    /** 最近错误 tick 及首次 UE 捕获 tick。 */
    uint64_t tick = UINT64_MAX, captured = UINT64_MAX;
    /** 当前 tick 的硬件置位和 CE 数量。 */
    uint32_t setThisTick = 0, correctedThisTick = 0;

    /** 记录 CE 或 UE，保持同 tick 最小 UE 地址。 */
    void
    record(uint64_t at, bool ue, uint64_t now)
    {
        if (tick != now) {
            tick = now;
            setThisTick = correctedThisTick = 0;
        }
        unsigned bit = ue ? 2 : 1;
        if (ue && !(status & 2)) {
            address = at;
            captured = now;
        } else if (ue && captured == now) {
            address = std::min(address, at);
        }
        status |= bit;
        setThisTick |= bit;
        if (!ue) {
            if (count != UINT32_MAX) {
                ++count;
            }
            ++correctedThisTick;
        }
    }
    /** 软件 RW1C 清除，保留同 tick 的硬件置位。 */
    void
    clear(uint32_t mask, uint64_t now)
    { status = (status & ~mask) | (tick == now ? setThisTick : 0); }
    /** 清零历史 CE 计数，保留同 tick 新发生的 CE。 */
    void
    clearCount(uint64_t now)
    { count = tick == now ? correctedThisTick : 0; }
    /** 阈值非零且累计 CE 达到阈值时拉高中断。 */
    bool
    ceIrq() const
    { return threshold && count >= threshold; }
};
} // namespace gem5::gw100
