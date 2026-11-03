/**
 * @file  mgmt_link.cc
 * @brief Gw100MgmtLink 的实现
 *
 * @see spec-mgmt-link
 */

#include "src/gw100/ml/mgmt_link.hh"

#include "base/logging.hh"

namespace gem5
{
namespace gw100
{

namespace
{

/** 按参数生成一端的配置 */
MlEndpoint::Config
makeConfig(const Gw100MgmtLinkParams &p, bool mgmt_side)
{
    MlEndpoint::Config c;
    c.tags = mgmt_side ? p.mgmt_tags : p.hif_tags;
    c.timeout = p.timeout;
    c.mgmtSide = mgmt_side;
    c.syncStages = p.sync_stages;
    return c;
}

} // anonymous namespace

/** 构造：owner 提供时钟与事件队列，ep 是被驱动的一端 */
ClockedMlClock::ClockedMlClock(ClockedObject &owner, MlEndpoint &ep)
    : owner(owner), ep(ep),
      event([this] { this->ep.process(); }, ep.name + ".process")
{
}

/** 当前时刻 */
Tick
ClockedMlClock::now() const
{
    return curTick();
}

/** 时刻 t 之后（不含 t）本端的第一个时钟沿 */
Tick
ClockedMlClock::edgeAfter(Tick t) const
{
    Tick p = owner.clockPeriod();
    return (t / p + 1) * p;
}

/** 本端当前的时钟周期 */
Tick
ClockedMlClock::period() const
{
    return owner.clockPeriod();
}

/** 请求在 when 时刻（不早于下一个时钟沿）调用一次本端的处理函数 */
void
ClockedMlClock::wakeAt(Tick when)
{
    Tick p = owner.clockPeriod();
    Tick t = std::max(when, curTick());
    if (t % p)
        t = (t / p + 1) * p;
    if (event.scheduled()) {
        if (event.when() <= t)
            return;
        owner.reschedule(event, t);
    } else {
        owner.schedule(event, t);
    }
}

/** 按参数构造两端和四个通道 */
Gw100MgmtLink::Gw100MgmtLink(const Params &p)
    : SimObject(p),
      h2mReq(p.req_fifo_beats, p.sync_stages),
      h2mRsp(p.rsp_fifo_beats, p.sync_stages),
      m2hReq(p.req_fifo_beats, p.sync_stages),
      m2hRsp(p.rsp_fifo_beats, p.sync_stages),
      hif(name() + ".hif", makeConfig(p, false)),
      mgmt(name() + ".mgmt", makeConfig(p, true))
{
}

/**
 * 所属对象在 init() 中调用：用 owner 的时钟驱动 side 这一端，
 * tgt 接收对端发来的请求；返回这一端的适配器
 */
MlEndpoint &
Gw100MgmtLink::attach(Side side, ClockedObject &owner, MlTargetIf &tgt)
{
    if (side == Side::Hif) {
        fatal_if(hifClk, "%s: HIF 侧被接入两次", name());
        hifClk = std::make_unique<ClockedMlClock>(owner, hif);
        hifTgt = &tgt;
    } else {
        fatal_if(mgmtClk, "%s: 管理 Tile 侧被接入两次", name());
        mgmtClk = std::make_unique<ClockedMlClock>(owner, mgmt);
        mgmtTgt = &tgt;
    }
    if (hifClk && mgmtClk) {
        h2mReq.bind(hifClk.get(), mgmtClk.get());
        h2mRsp.bind(hifClk.get(), mgmtClk.get());
        m2hReq.bind(mgmtClk.get(), hifClk.get());
        m2hRsp.bind(mgmtClk.get(), hifClk.get());
        hif.bind(hifClk.get(), hifTgt, &h2mReq, &h2mRsp, &m2hReq,
                 &m2hRsp, &mgmt);
        mgmt.bind(mgmtClk.get(), mgmtTgt, &m2hReq, &m2hRsp, &h2mReq,
                  &h2mRsp, &hif);
    }
    return side == Side::Hif ? hif : mgmt;
}

/** 所属对象在 startup() 中调用：撤销 side 这一端的复位 */
void
Gw100MgmtLink::release(Side side)
{
    fatal_if(!hifClk || !mgmtClk, "%s: 两端尚未都接入", name());
    (side == Side::Hif ? hif : mgmt).resetRelease();
}

} // namespace gw100
} // namespace gem5
