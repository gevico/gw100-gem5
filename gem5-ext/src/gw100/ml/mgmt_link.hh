/**
 * @file  mgmt_link.hh
 * @brief Gw100MgmtLink：把 ML 的两端接到各自所属的 ClockedObject 上
 *
 * 两端各由一个 ClockedObject 驱动：HIF 侧是 PCIe 设备（clk_pcie），
 * 管理 Tile 侧是管理 Tile（clk_mgmt）。所属对象在 init() 中调用
 * attach()，在 startup() 中调用 release() 撤销本端复位。
 *
 * @see spec-mgmt-link
 */

#ifndef __GW100_ML_MGMT_LINK_HH__
#define __GW100_ML_MGMT_LINK_HH__

#include <memory>

#include "params/Gw100MgmtLink.hh"
#include "sim/clocked_object.hh"
#include "sim/eventq.hh"
#include "sim/sim_object.hh"
#include "src/gw100/ml/ml_endpoint.hh"

namespace gem5
{
namespace gw100
{

/** 用一个 ClockedObject 驱动一端 MlEndpoint 的时钟适配器 */
class ClockedMlClock : public MlClockIf
{
  public:
    /** 构造：owner 提供时钟与事件队列，ep 是被驱动的一端 */
    ClockedMlClock(ClockedObject &owner, MlEndpoint &ep);

    /** 当前时刻 */
    Tick now() const override;

    /** 时刻 t 之后（不含 t）本端的第一个时钟沿 */
    Tick edgeAfter(Tick t) const override;

    /** 本端当前的时钟周期 */
    Tick period() const override;

    /** 请求在 when 时刻（不早于下一个时钟沿）调用一次本端的处理函数 */
    void wakeAt(Tick when) override;

  private:
    /** 提供时钟与事件队列的对象 */
    ClockedObject &owner;
    /** 被驱动的一端 */
    MlEndpoint &ep;
    /** 调用 ep.process() 的事件 */
    EventFunctionWrapper event;
};

/** 管理链路 ML：两端适配器与四个异步通道 */
class Gw100MgmtLink : public SimObject
{
  public:
    PARAMS(Gw100MgmtLink);

    /** 按参数构造两端和四个通道 */
    explicit Gw100MgmtLink(const Params &p);

    /** 哪一端 */
    enum class Side
    {
        /** HIF 侧（PCIe Tile） */
        Hif,
        /** 管理 Tile 侧 */
        Mgmt,
    };

    /**
     * 所属对象在 init() 中调用：用 owner 的时钟驱动 side 这一端，
     * tgt 接收对端发来的请求；返回这一端的适配器
     */
    MlEndpoint &attach(Side side, ClockedObject &owner, MlTargetIf &tgt);

    /** 所属对象在 startup() 中调用：撤销 side 这一端的复位 */
    void release(Side side);

    /**
     * 复位 side 这一端（设备完全复位时两端都复位，spec-mgmt-link
     * §7.6）：在途事务丢弃，不再回调；由 release() 撤销
     */
    void reset(Side side);

  private:
    /** HIF → 管理 Tile 请求通道 */
    MlEndpoint::ReqFifo h2mReq;
    /** HIF → 管理 Tile 响应通道 */
    MlEndpoint::RspFifo h2mRsp;
    /** 管理 Tile → HIF 请求通道 */
    MlEndpoint::ReqFifo m2hReq;
    /** 管理 Tile → HIF 响应通道 */
    MlEndpoint::RspFifo m2hRsp;
    /** HIF 侧适配器 */
    MlEndpoint hif;
    /** 管理 Tile 侧适配器 */
    MlEndpoint mgmt;
    /** HIF 侧的时钟适配器 */
    std::unique_ptr<ClockedMlClock> hifClk;
    /** 管理 Tile 侧的时钟适配器 */
    std::unique_ptr<ClockedMlClock> mgmtClk;
    /** HIF 侧的目标 */
    MlTargetIf *hifTgt = nullptr;
    /** 管理 Tile 侧的目标 */
    MlTargetIf *mgmtTgt = nullptr;
};

} // namespace gw100
} // namespace gem5

#endif // __GW100_ML_MGMT_LINK_HH__
