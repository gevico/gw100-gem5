/**
 * @file  memory_link.cc
 * @brief MemoryLink 与 MemoryClock 的实现
 *
 * @see spec-dram, spec-clock-reset-power
 */

#include "src/gw100/mem_tile/memory_link.hh"

#include <algorithm>
#include "base/trace.hh"
#include "debug/Gw100Memory.hh"

#include "src/gw100/mem_tile/generated/memory_defs.hh"

namespace gem5::gw100
{

namespace md = memorydefs;

/** 绑定实际时钟域和处理函数 */
MemoryClock::MemoryClock(ClockedObject &owner, ClockDomain &domain,
                         const std::string &name,
                         std::function<void()> process)
    : Clocked(domain),
      owner(owner),
      event(
          [this, process = std::move(process)] {
              if (clockEdge() != curTick()) {
                  wakeAt(clockEdge());
                  return;
              }
              process();
          },
          name)
{}

/** 时刻 t 之后的第一个实际时钟沿 */
Tick
MemoryClock::edgeAfter(Tick t) const
{
    Tick edge = clockEdge();
    if (edge > t) {
        return edge;
    }
    return edge + ((t - edge) / clockPeriod() + 1) * clockPeriod();
}

/** 在指定时刻或下一个时钟沿请求唤醒 */
void
MemoryClock::wakeAt(Tick when)
{
    Tick edge = edgeAfter(curTick());
    if (when > edge) {
        edge += divCeil(when - edge, clockPeriod()) * clockPeriod();
    }
    when = edge;
    if (!event.scheduled()) {
        owner.schedule(event, when);
    } else if (when < event.when()) {
        owner.reschedule(event, when);
    }
}

/** 构造有限槽位的链路 */
MemoryLink::MemoryLink(ClockedObject &owner, ClockDomain &source,
                       ClockDomain &target, const std::string &name,
                       unsigned limit)
    : linkName(name),
      limit(limit),
      sourceClock(owner, source, name + ".source", [this] { sourceStep(); }),
      targetClock(owner, target, name + ".target", [this] { targetStep(); }),
      requests(md::FIFO_DEPTH, md::SYNC_STAGES),
      responses(md::FIFO_DEPTH, md::SYNC_STAGES)
{
    requests.bind(&sourceClock, &targetClock);
    responses.bind(&targetClock, &sourceClock);
}

/** 提交已经取得槽位的请求 */
void
MemoryLink::submit(const ItemPtr &item)
{
    panic_if(!canSubmit(), "Memory CDC slots exhausted");
    ++used;
    item->queuedAt = curTick();
    item->requestBeats =
        1 + (item->packet->isWrite()
                 ? divCeil(item->packet->getSize(), unsigned(md::FIFO_BYTES))
                 : 0);
    outgoing.push_back(item);
    sourceClock.wakeAt(item->availableAt);
}

/** 目标已取得响应，开始返回传输 */
void
MemoryLink::respond(const ItemPtr &item)
{
    item->sent = 0;
    returning.push_back(item);
    targetClock.wakeAt(curTick());
}

/** 上游已撤销返回反压 */
void
MemoryLink::retryResponse()
{
    responseBlocked = false;
    sourceClock.wakeAt(curTick());
}

/** 在下一个目标时钟沿处理媒体进度 */
void
MemoryLink::wakeTarget()
{ targetClock.wakeAt(curTick()); }

/** 数据与同步信用均已排空 */
bool
MemoryLink::idle()
{
    return !used && !requests.occupancy(curTick()) &&
           !responses.occupancy(curTick());
}

/** 处理源时钟端 */
void
MemoryLink::sourceStep()
{
    if (!outgoing.empty()) {
        auto item = outgoing.front();
        if (item->availableAt > curTick()) {
            sourceClock.wakeAt(item->availableAt);
        } else if (requests.canPush(curTick())) {
            if (!item->sent) {
                queueTicks += curTick() - item->queuedAt;
            }
            bool last = ++item->sent == item->requestBeats;
            DPRINTF(Gw100Memory, "%llu request %#x beat %u/%u\n",
                    sourceClock.period(), item->packet->getAddr(), item->sent,
                    item->requestBeats);
            requests.push(item, item->sent - 1, last, curTick());
            cdcTicks += targetClock.edgeAfter(curTick()) - curTick() +
                        (md::SYNC_STAGES - 1) * targetClock.period();
            transferTicks += sourceClock.period();
            if (last) {
                outgoing.pop_front();
            }
            if (!outgoing.empty()) {
                sourceClock.wakeAt(curTick());
            }
        } else {
            Tick next = requests.nextFreeAt(curTick());
            if (next != MaxTick) {
                sourceClock.wakeAt(next);
            }
        }
    }
    if (!responseBlocked && responses.frontVisible(curTick())) {
        auto beat = responses.front();
        Addr address = beat.payload->packet->getAddr();
        if (beat.last && !beat.payload->complete()) {
            responseBlocked = true;
        } else {
            responses.pop(curTick());
            if (beat.last) {
                DPRINTF(Gw100Memory, "complete %#x\n", address);
                beat.payload->complete = {};
                --used;
            }
        }
    }
    if (!responseBlocked && !responses.empty()) {
        sourceClock.wakeAt(responses.front().visibleAt);
    }
    if (onProgress) {
        onProgress();
    }
}

/** 处理目标时钟端 */
void
MemoryLink::targetStep()
{
    if (requests.frontVisible(curTick())) {
        auto beat = requests.front();
        requests.pop(curTick());
        if (beat.last) {
            auto receive = std::move(beat.payload->receive);
            receive();
        }
    }
    if (!requests.empty()) {
        targetClock.wakeAt(requests.front().visibleAt);
    }
    if (!returning.empty()) {
        auto item = returning.front();
        if (responses.canPush(curTick())) {
            unsigned beats =
                1 + (item->packet->isRead() ? divCeil(item->packet->getSize(),
                                                      unsigned(md::FIFO_BYTES))
                                            : 0);
            bool last = ++item->sent == beats;
            responses.push(item, item->sent - 1, last, curTick());
            cdcTicks += sourceClock.edgeAfter(curTick()) - curTick() +
                        (md::SYNC_STAGES - 1) * sourceClock.period();
            transferTicks += targetClock.period();
            if (last) {
                returning.pop_front();
            }
            if (!returning.empty()) {
                targetClock.wakeAt(curTick());
            }
        } else {
            Tick next = responses.nextFreeAt(curTick());
            if (next != MaxTick) {
                targetClock.wakeAt(next);
            }
        }
    }
    if (onTargetProgress) {
        onTargetProgress();
    }
    if (onProgress) {
        onProgress();
    }
}

} // namespace gem5::gw100
