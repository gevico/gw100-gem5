/**
 * @file ct_sync.hh
 * @brief compute tile 内部的 sync：作为硬件同步器同步下游矩阵引擎和向量引擎
 *
 * @see spec-ct
 */

#ifndef __GW100_CT_SYNC_CT_SYNC_HH__
#define __GW100_CT_SYNC_CT_SYNC_HH__

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

#include "src/gw100/ct/ct_if.hh"
#include "src/gw100/ct/ct_types.hh"

namespace gem5
{
namespace gw100
{
class CtSync : public CtWindowHandler
{
  public:
    /** sync 内部寄存器个数 */
    static constexpr unsigned NumCounters = 32;
    /** sync 单个寄存器大小 共 32 位 */
    static constexpr unsigned CounterWides = 4;

    explicit CtSync(CtClockIf &clock_if);

    /** override 上级对象 virtual 契约 */
    ~CtSync() override = default;
    /** 核心访问机制,其他 tile 或者内部管理 cpu 进行访问 */
    CtAccessResult access(CtAccess &a, Tick now) override;
    /** 清空内部同步寄存器 */
    void reset() override;
    /** 记录 sync 内部计数器的调用 */
    Tick schedule(Addr a, unsigned size, bool write, Tick now,
                  Tick period);

    /** 本地通讯接口，计数器充当临时储存队列指针，由上层调用保证不溢出 */
    /** 将对应 idx 的计数器进行加 delta，需判断溢出 */
    bool signal(unsigned idx, uint32_t delta = 1);
    /** 校验特定 idx 的计数器数值是否大于等于 expected 并消费 */
    bool tryWait(unsigned idx, uint32_t expected = 1);

  private:
    CtClockIf &clockIf;
    /** 记录内部同步寄存器状态，进行同步判断 */
    std::array<std::uint32_t, NumCounters> syncs;
    /** 记录各同步寄存器端口使用情况，进行当下 tick 的判断 */
    std::array<Tick, NumCounters> syncsFree;
    /** 归一化，得到 Tile 窗口内部偏移 */
    static Addr normalizeAddr(Addr addr);
};
}
}

#endif
