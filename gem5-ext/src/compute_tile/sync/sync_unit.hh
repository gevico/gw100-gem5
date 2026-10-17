/**
 * 同步单元（SPEC-250）：一组计数器，供功能单元之间做生产者-消费者同步。
 * - signal(c)：counter[c] += 1；
 * - wait(c, v, cb)：等到 counter[c] >= v 再调用 cb。
 * 条件满足后，回调在 latency 个周期之后执行，模拟同步的硬件延迟。
 */

#ifndef __ACIS_COMPUTE_TILE_SYNC_SYNC_UNIT_HH__
#define __ACIS_COMPUTE_TILE_SYNC_SYNC_UNIT_HH__

#include <functional>
#include <list>
#include <vector>

#include "base/statistics.hh"
#include "params/AcisSyncUnit.hh"
#include "sim/clocked_object.hh"

namespace gem5
{
namespace acis
{

class SyncUnit : public ClockedObject
{
  public:
    using Callback = std::function<void()>;

    PARAMS(AcisSyncUnit);
    SyncUnit(const Params &p);

    void wait(unsigned cnt, uint32_t val, Callback cb);
    void signal(unsigned cnt);
    uint32_t value(unsigned cnt) const;

  private:
    struct Waiter
    {
        unsigned cnt;
        uint32_t val;
        Callback cb;
    };

    void fire(Callback cb);
    void checkIndex(unsigned cnt) const;

    std::vector<uint32_t> counters;
    std::list<Waiter> waiters;
    const Cycles latency;

    struct SyncStats : public statistics::Group
    {
        SyncStats(SyncUnit &s);
        statistics::Scalar signals;
        statistics::Scalar waits;
        statistics::Scalar blockedWaits;
    } syncStats;
};

} // namespace acis
} // namespace gem5

#endif // __ACIS_COMPUTE_TILE_SYNC_SYNC_UNIT_HH__
