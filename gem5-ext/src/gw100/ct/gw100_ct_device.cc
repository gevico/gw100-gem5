/**
 * @file  gw100_ct_device.cc
 * @brief Compute Tile 的实现
 *
 * @see spec-ct
 */

#include "src/gw100/ct/gw100_ct_device.hh"

#include "base/logging.hh"
#include "src/gw100/ct/ct_types.hh"
#include "src/gw100/ct/ct_windows.hh"
#include "src/gw100/ct/ctrl/ct_ctrl_ram.hh"

namespace gem5
{
namespace gw100
{

/** clock 子函数实现 */
/** 构造：owner 是所属设备，tick 在请求的时钟沿调用 */
CtClock::CtClock(ClockedObject &owner, std::function<void()> tick)
    : owner(owner), event(tick, owner.name() + ".ct")
{
}

/** 在不早于 when 的第一个时钟沿调用 tick */
void
CtClock::wakeAt(Tick when)
{
    Tick e = owner.clockEdge();
    if (when > e)
        e = owner.clockEdge(owner.ticksToCycles(when - e));
    if (!event.scheduled())
        owner.schedule(event, e);
    else if (event.when() > e)
        owner.reschedule(event, e);
}

/** pio 子函数实现 */
CtPioPort::CtPioPort(const std::string &name, ClockedObject &owner,
                     CtWindowHandler &window, Addr window_base)
    : QueuedResponsePort(name, queue),
      owner(owner),
      queue(owner, *this),
      window(window),
      windowBase(window_base)
{
}

CtAccessResult
CtPioPort::doAccess(PacketPtr pkt, Tick now)
{
    CtAccess a;
    a.base = pkt->getAddr();
    a.data = pkt->getPtr<uint8_t>();
    a.write = pkt->isWrite();
    /** todo 后续需要根据外部 pkt 的实际情况进行替换 */
    a.initiator = CtInitiator::MT;
    a.size = pkt->getSize();
    /** 调用对应 windows 接口访问 */
    return window.access(a, now);
}

/** 仿照 mgmt 逻辑 */
AddrRangeList
CtPioPort::getAddrRanges() const
{
    AddrRangeList r;
    r.push_back(RangeSize(windowBase, CtWindowSize));
    return r;
}

/** 时序调用，无论成功失败都需要按周期数进行返回处理 */
bool
CtPioPort::recvTimingReq(PacketPtr pkt)
{
    Tick now = owner.clockEdge();
    CtAccessResult res = doAccess(pkt, now);
    pkt->makeResponse();
    if (res.status != CtStatus::Ok) {
        /** 失败调用 pkt 函数进行返回并在下一拍发回错误响应 */
        pkt->setBadAddress();
        schedTimingResp(pkt, owner.clockEdge(Cycles(1)));
    } else {
        /** 成功正常发回计算好了 delay */
        schedTimingResp(pkt, now + res.delay);
    }
    return true;
}

/** 原子调用，只返回耗时 */
Tick
CtPioPort::recvAtomic(PacketPtr pkt)
{
    CtAccessResult res = doAccess(pkt, owner.clockEdge());
    pkt->makeResponse();
    if (res.status != CtStatus::Ok) {
        pkt->setBadAddress();
        return owner.clockPeriod();
    }
    return res.delay;
}

void
CtPioPort::recvFunctional(PacketPtr pkt)
{
    CtAccessResult res = doAccess(pkt, curTick());
    pkt->makeResponse();
    if (res.status != CtStatus::Ok) {
        pkt->setBadAddress();
    }
}

/** 主类函数 */
Gw100Ct::Gw100Ct(const Params &p)
    : ClockedObject(p),
      windowBase(p.window_base),
      clock(*this, [this] { tick(); }),
      sram(clock),
      sync(clock),
      ctrl_ram(clock),
      window(sram, sync,ctrl_ram, windowBase),
      pio(name() + ".pio", *this, window, windowBase)
{
}

/** todo 目前先占位，等 ni-dma 完成后处理 */
void
Gw100Ct::tick()
{
}

Port &
Gw100Ct::getPort(const std::string &if_name, PortID idx)
{
    if (if_name == "pio") {
        return pio;
    }
    return ClockedObject::getPort(if_name, idx);
}

void
Gw100Ct::init()
{
    ClockedObject::init();
    fatal_if(!pio.isConnected(), "%s: pio 端口没有连接", name());
    pio.sendRangeChange();
}

} 
} 
