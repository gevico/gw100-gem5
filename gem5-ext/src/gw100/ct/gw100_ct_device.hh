/**
 * @file  gw100_ct_device.hh
 * @brief Compute Tile 的 SimObject 外壳与时钟、端口
 *
 * @see spec-ct
 */

#ifndef __GW100_CT_GW100_CT_DEVICE_HH__
#define __GW100_CT_GW100_CT_DEVICE_HH__

#include <functional>
#include <string>

#include "mem/qport.hh"
#include "params/Gw100Ct.hh"
#include "sim/clocked_object.hh"
#include "sim/eventq.hh"
#include "src/gw100/ct/ct_if.hh"
#include "src/gw100/ct/ct_types.hh"
#include "src/gw100/ct/ct_windows.hh"
#include "src/gw100/ct/sram/ct_sram.hh"
#include "src/gw100/ct/sync/ct_sync.hh"

namespace gem5
{
namespace gw100
{
/** 把 gem5 ClockedObject 的时钟包装成 CtClockIf，用回调唤醒主类 */
class CtClock : public CtClockIf
{
  public:
    CtClock(ClockedObject &owner, std::function<void()> tick_cb);

    Tick now() const override { return curTick(); }
    Tick period() const override { return owner.clockPeriod(); }
    void wakeAt(Tick when) override;

  private:
    ClockedObject &owner;
    EventFunctionWrapper event;
};

/** pio 接口，将 gem5 的三种模式进行定义 */
class CtPioPort : public QueuedResponsePort
{
  public:
    CtPioPort(const std::string &name, ClockedObject &owner,
              CtWindowHandler &window, Addr window_base);

    /** 基类要求的 4 个接口 */
    AddrRangeList getAddrRanges() const override;
    bool recvTimingReq(PacketPtr pkt) override;
    Tick recvAtomic(PacketPtr pkt) override;
    void recvFunctional(PacketPtr pkt) override;

  private:
    /** 把 pkt 打包成 CtAccess 并调用 window.access */
    CtAccessResult doAccess(PacketPtr pkt, Tick now);

    ClockedObject &owner;
    RespPacketQueue queue;
    CtWindowHandler &window;
    Addr windowBase;
};
class Gw100Ct : public ClockedObject
{
  public:
    PARAMS(Gw100Ct);

    explicit Gw100Ct(const Params &p);

    Port &getPort(const std::string &if_name,
                  PortID idx = InvalidPortID) override;
    void init() override;

  private:
    /** 供 CtClock 定时器到期时回调的入口*/
    void tick();

    /** 注意声明顺序决定构造顺序 */
    const Addr windowBase;
    CtClock clock;
    CtSram sram;
    CtSync sync;
    CtWindow window;
    CtPioPort pio;
};

} 
} 

#endif 
