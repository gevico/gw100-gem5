/**
 * @file  hdma_glue.hh
 * @brief HdmaEngine 接入 gem5：时钟、host DMA 端口、ML 客户端
 *
 * - HdmaClock：用所属设备的时钟（clk_pcie）驱动 HdmaEngine::tick()。
 * - HostDmaPort：PCIe 设备的 dma 端口。一个读或写请求（TLP）按 cache
 *   行拆成若干个内存包，全部响应到达时完成；读收到错误响应按 UR 处理。
 *   已交给发送通路、尚未收到全部响应的写请求数不超过 tx_credits，近似
 *   replay buffer 的容量（spec-pcie §10.4）。
 * - MlHdmaClient：HDMA 作为 ML HIF 侧的一个客户端，发出 DESC_RD、
 *   DESC_WR 和 EVENT（spec-mgmt-link §4）。
 *
 * @see spec-host-dma, spec-pcie, spec-mgmt-link
 */

#ifndef __GW100_PCIE_HDMA_HDMA_GLUE_HH__
#define __GW100_PCIE_HDMA_HDMA_GLUE_HH__

#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "mem/port.hh"
#include "sim/clocked_object.hh"
#include "sim/eventq.hh"
#include "src/gw100/ml/ml_endpoint.hh"
#include "src/gw100/pcie/hdma/hdma_if.hh"

namespace gem5
{
namespace gw100
{

/** 用所属设备的时钟驱动 HDMA */
class HdmaClock : public HdmaClockIf
{
  public:
    /** 构造：owner 是所属设备，tick 在请求的时钟沿调用 */
    HdmaClock(ClockedObject &owner, std::function<void()> tick);

    /** 当前时刻 */
    Tick now() const override { return curTick(); }

    /** 时钟周期 */
    Tick period() const override { return owner.clockPeriod(); }

    /** 在不早于 when 的第一个时钟沿调用 tick */
    void wakeAt(Tick when) override;

  private:
    /** 所属设备 */
    ClockedObject &owner;
    /** 调用 tick 的事件 */
    EventFunctionWrapper event;
};

/** PCIe 设备的 dma 端口：HDMA 访问 host 内存 */
class HostDmaPort : public RequestPort, public HdmaHostIf
{
  public:
    /**
     * 构造：id 为请求者 ID，line 为内存包的最大字节数（cache 行），
     * tx_credits 为在途写请求的上限
     */
    HostDmaPort(const std::string &name, RequestorID id, unsigned line,
                unsigned tx_credits);

    /** 总线地址到 host 物理地址的转换；不设置时不转换 */
    std::function<Addr(Addr)> toPhys;

    /** 在途写请求减少、可以再写时调用 */
    std::function<void()> onTxFree;

    /** 发出一个 MRd */
    void read(Addr addr, unsigned len, HostReadDone done) override;

    /** 发送通路能否再接受一个写请求 */
    bool canWrite() const override { return txInFlight < txCredits; }

    /** 发出一个 MWr */
    void write(Addr addr, unsigned len, const uint8_t *data) override;

  protected:
    /** 内存包的响应 */
    bool recvTimingResp(PacketPtr pkt) override;

    /** 内存系统可以再接受请求 */
    void recvReqRetry() override;

  private:
    /** 一个读或写请求 */
    struct Op
    {
        /** 是否为写 */
        bool write = false;
        /** 读的数据 */
        std::vector<uint8_t> buf;
        /** 尚未收到响应的内存包数 */
        unsigned remaining = 0;
        /** 是否收到过错误响应 */
        bool err = false;
        /** 读完成时的回调 */
        HostReadDone done;
    };

    /** 内存包携带的状态：所属请求与它在请求中的偏移 */
    struct State : public Packet::SenderState
    {
        /** 所属请求 */
        std::shared_ptr<Op> op;
        /** 在请求中的偏移 */
        unsigned off = 0;
    };

    /** 把 [addr, addr + len) 拆成内存包排队发出 */
    void split(const std::shared_ptr<Op> &op, MemCmd cmd, Addr addr,
               unsigned len, const uint8_t *data);

    /** 按顺序发出排队的内存包，直到被反压 */
    void trySend();

    /** 请求者 ID */
    const RequestorID requestorId;
    /** 内存包的最大字节数 */
    const unsigned line;
    /** 在途写请求的上限 */
    const unsigned txCredits;
    /** 在途写请求数 */
    unsigned txInFlight = 0;
    /** 排队等待发出的内存包 */
    std::deque<PacketPtr> sendQ;
    /** 是否在等待重试 */
    bool waitRetry = false;
};

/** HDMA 作为 ML HIF 侧的客户端 */
class MlHdmaClient : public HdmaMlIf
{
  public:
    /** 接入 ML 的 HIF 侧适配器，登记一个客户端 */
    void bind(MlEndpoint *ep);

    /** 读 32 B 的描述符 */
    bool descRead(Addr addr, DescReadDone done) override;

    /** non-posted 回写 4 B 的 DW7 */
    bool descWrite(Addr addr, uint32_t dw7, DescWriteDone done) override;

    /** posted 的 EVENT */
    bool event(uint32_t bits) override;

    /** 撤销 HDMA 已被 ML 接受的全部请求 */
    void withdraw() override;

  private:
    /** ML 的 HIF 侧适配器；为空时不接管理 Tile */
    MlEndpoint *ml = nullptr;
    /** HDMA 的客户端编号 */
    unsigned client = 0;
};

} // namespace gw100
} // namespace gem5

#endif // __GW100_PCIE_HDMA_HDMA_GLUE_HH__
