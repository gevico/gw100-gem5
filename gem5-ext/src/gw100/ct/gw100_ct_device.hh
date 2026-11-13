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
#include "src/gw100/ct/ctrl/ct_ctrl_ram.hh"
#include "src/gw100/ct/compute/ct_fc32.hh"
#include "src/gw100/ct/compute/ct_matrix_engine.hh"
#include "src/gw100/ct/compute/ct_vector_engine.hh"
#include "src/gw100/ct/nidma/ct_nidma.hh"
#include "src/gw100/ct/ctrl/ct_ctrl.hh"
namespace gem5
{
namespace gw100
{

/** 内部辅助类 */
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


/** 被动相应端口，将 gem5 的三种模式进行定义，pio 作为 response port 对外部请求进行相应调用内部 windows 接口 */
class CtPioPort : public QueuedResponsePort
{
  public:
    CtPioPort(const std::string &name, ClockedObject &owner,
              CtWindowHandler &window, Addr window_base);

    /** 基类要求的 4 个接口 */
    /** 注册地址表，gem5 保证地址不重合 */
    AddrRangeList getAddrRanges() const override;
    /** 对数据包的 req 进行一个处理 */
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

/** 主动发送端口，相当于 pio 和 windows 的集合，但是发数据包不需要复杂的数据地址管理 */
class CtDmaPort : public QueuedRequestPort,public CtNocIf
{
  public:
    CtDmaPort(const std::string &name, ClockedObject &owner);
    
    struct Callback : public Packet::SenderState{
      CtNocReadDone read;
      CtNocWriteDone write; 
    };
    /** 将 nidma 的主动发包给外层方法 */
    void sendPacket(Addr dpa, const uint8_t *data, unsigned len, bool is_write, Callback *back);
    /** CtNocIf 的上层函数契约，实现发送功能 */
    ~CtDmaPort() = default;
    bool canSend() const override;
    void sendRead(Addr dpa, unsigned len, CtNocReadDone done)  override;
    void sendWrite(Addr dpa, unsigned len, const uint8_t *data,CtNocWriteDone done) override;

    /** QueuedRequestPort的上层契约，处理发送后的结果相应 */
    void recvReqRetry() override { reqQueue.retry(); }
    bool recvTimingResp(PacketPtr pkt) override;

  private:
    ClockedObject &owner;
    ReqPacketQueue queue;
    /** gw100 无 cache，该队列仅仅实现函数契约而非实际使用 */
    SnoopRespPacketQueue snoop_queue;

};

/** 主类函数，作为核心聚合单元 */
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
    /** 应该按照各个功能化的初始顺序进行排列 */
    const Addr windowBase;
    /** 只依赖外壳 */
    CtClock clock;
    /** 储存和状态构建，依赖时钟或者没有依赖 */
    CtSram sram;
    CtSync sync;
    CtCtrlSram ctrl_ram;
    CtFc32 ct_fc32;
    /** gem5 与 gw100 的边界交互，window 作为上层依赖 */
    CtWindow window;
    CtPioPort pio;
    CtDmaPort dma_port;
    /** 计算单元和控制核，控制核作为最后的初始化 */
    CtNidma ct_nidma;
    CtMatrix ct_matrix;
    CtVector ct_vector;
    CtCtrl ct_ctrl;
};

} 
} 

#endif 
