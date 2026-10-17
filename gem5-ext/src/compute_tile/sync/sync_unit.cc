/**
 * @file  sync_unit.cc
 * @brief SyncUnit 的实现
 *
 * @see SPEC-250
 */

#include "src/compute_tile/sync/sync_unit.hh"

#include "base/logging.hh"
#include "sim/eventq.hh"

namespace gem5
{
namespace acis
{

SyncUnit::SyncUnit(const Params &p)
    : ClockedObject(p), counters(p.counters, 0), latency(p.latency), syncStats(*this)
{
}

void
SyncUnit::checkIndex(unsigned cnt) const
{
    panic_if(cnt >= counters.size(), "%s: 计数器编号 %u 超出范围（共 %u 个）", name(), cnt,
             counters.size());
}

void
SyncUnit::fire(Callback cb)
{
    schedule(new EventFunctionWrapper(std::move(cb), name() + ".wake", true),
             clockEdge(latency));
}

void
SyncUnit::wait(unsigned cnt, uint32_t val, Callback cb)
{
    checkIndex(cnt);
    syncStats.waits++;
    if (counters[cnt] >= val) {
        fire(std::move(cb));
    } else {
        syncStats.blockedWaits++;
        waiters.push_back({cnt, val, std::move(cb)});
    }
}

void
SyncUnit::signal(unsigned cnt)
{
    checkIndex(cnt);
    syncStats.signals++;
    counters[cnt]++;
    for (auto it = waiters.begin(); it != waiters.end();) {
        if (it->cnt == cnt && counters[cnt] >= it->val) {
            fire(std::move(it->cb));
            it = waiters.erase(it);
        } else {
            ++it;
        }
    }
}

uint32_t
SyncUnit::value(unsigned cnt) const
{
    checkIndex(cnt);
    return counters[cnt];
}

SyncUnit::SyncStats::SyncStats(SyncUnit &s)
    : statistics::Group(&s, "sync"),
      ADD_STAT(signals, statistics::units::Count::get(), "计数器更新次数"),
      ADD_STAT(waits, statistics::units::Count::get(), "等待请求次数"),
      ADD_STAT(blockedWaits, statistics::units::Count::get(), "需要阻塞等待的次数")
{
}

} // namespace acis
} // namespace gem5
