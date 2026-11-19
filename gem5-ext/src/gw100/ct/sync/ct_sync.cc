/**
 * @file ct_sync.cc
 * @brief compute tile 内部的 sync：作为硬件同步器同步下游矩阵引擎和向量引擎
 *
 * @see spec-ct
 */
#include "src/gw100/ct/sync/ct_sync.hh"

#include <algorithm>

#include "src/gw100/ct/ct_types.hh"

namespace gem5
{
namespace gw100
{
CtSync::CtSync(CtClockIf &clock_if)
    : clockIf(clock_if), syncs{}, syncsFree{}
{
}

void
CtSync::reset()
{
    syncs.fill(0);
    syncsFree.fill(0);
}

CtAccessResult
CtSync::access(CtAccess &a, Tick now)
{
    CtAccessResult res;

    /** access 出现非法字段 fault */
    if (a.size == 0 || a.data == nullptr) {
        res.status = CtStatus::Fault;
        return res;
    }

    Addr offset = normalizeAddr(a.base);

    /** 检查越界 */
    if ((offset >= CtSyncBase + CtSyncSize) || (offset < CtSyncBase)) {
        res.status = CtStatus::Reject;
        return res;
    }

    Addr sync_offset = offset - CtSyncBase;
    /** 检查是否按单个 4B 寄存器自然对齐访问 */
    if (a.size != CounterWides || (sync_offset % CounterWides) != 0) {
        res.status = CtStatus::Reject;
        return res;
    }

    /** 寄存器单元的读写 */
    unsigned idx = sync_offset >> 2;
    if (a.write) {
        uint32_t delta = 0;
        std::memcpy(&delta, a.data, sizeof(uint32_t));
        if (!signal(idx, delta)) {
            res.status = CtStatus::Reject;
            return res;
        }
    } else {
        std::memcpy(a.data, &syncs[idx], sizeof(uint32_t));
    }

    Tick cost = schedule(sync_offset, a.size, a.write, now, clockIf.period());
    res.status = CtStatus::Ok;
    res.delay = cost - now;
    return res;
}

Tick
CtSync::schedule(Addr a, unsigned size, bool write, Tick now, Tick period)
{
    /** 已经保证以 4 字节的形式进行读写了，不存在不对称读写情况 */
    unsigned idx = a >> 2;
    Tick start = std::max(now, syncsFree[idx]);
    /** 只访问一次单个寄存器 */
    syncsFree[idx] = start + 1 * period;
    return syncsFree[idx];
}

Addr
CtSync::normalizeAddr(Addr addr)
{
    return addr % CtWindowSize;
}

bool
CtSync::signal(unsigned idx, uint32_t delta)
{
    if (idx >= NumCounters) {
        return false;
    }
    /** 判断无符号加法是否溢出 */
    if ((syncs[idx] + delta) < delta) {
        return false;
    }
    syncs[idx] += delta;
    return true;
}

bool
CtSync::tryWait(unsigned idx, uint32_t expected)
{
    if (idx >= NumCounters) {
        return false;
    }
    if (syncs[idx] < expected) {
        return false;
    }
    syncs[idx] -= expected;
    return true;
}

}
}
