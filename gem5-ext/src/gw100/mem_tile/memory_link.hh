/**
 * @file  memory_link.hh
 * @brief 内存边界的有界双向 CDC 链路
 *
 * 请求与响应按 32 B 拍传送，复用 ML 的两级指针同步 FIFO。
 * 槽位保持到响应被上游接受，媒体服务由接收方回调执行。
 *
 * @see spec-dram, spec-clock-reset-power
 */

#ifndef GW100_MEMORY_LINK_HH
#define GW100_MEMORY_LINK_HH

#include <deque>
#include <functional>
#include <memory>
#include <string>

#include "mem/packet.hh"
#include "sim/clocked_object.hh"
#include "src/gw100/ml/async_fifo.hh"

namespace gem5::gw100
{

/** 带时钟相位的 FIFO 时钟端 */
class MemoryClock : public Clocked, public MlClockIf
{
  public:
    /** 绑定实际时钟域和处理函数 */
    MemoryClock(ClockedObject &owner, ClockDomain &domain,
                const std::string &name, std::function<void()> process);
    /** 当前仿真时刻 */
    Tick
    now() const override
    { return curTick(); }
    /** 时刻 t 之后的第一个实际时钟沿 */
    Tick edgeAfter(Tick t) const override;
    /** 当前时钟周期 */
    Tick
    period() const override
    { return clockPeriod(); }
    /** 在指定时刻或下一个时钟沿请求唤醒 */
    void wakeAt(Tick when) override;

  private:
    /** 提供事件队列的外壳 */
    ClockedObject &owner;
    /** 驱动链路的事件 */
    EventFunctionWrapper event;
};

/** 硬件入口附加的可信来源元数据 */
struct MemorySourceState : public Packet::SenderState
{
    /** 硬件入口编号 */
    unsigned source = 0;
    /** 来源事务号 */
    uint64_t transaction = 0;
    /** 内存复位代次 */
    uint64_t epoch = 0;
    /** 整个原始数据事务已经通过入口预检 */
    bool admitted = false;
    /** 来源仍属于当前功能代次 */
    std::function<bool()> valid;
};

/** 一个有界的跨域请求和返回链路 */
class MemoryLink
{
  public:
    /** 链路内的一次传输 */
    struct Item
    {
        /** 链路传输的包，由上层持有 */
        PacketPtr packet = nullptr;
        /** 包头和负载已到达发送端的时刻 */
        Tick availableAt = 0;
        /** 请求进入链路的时刻 */
        Tick queuedAt = 0;
        /** 请求所占拍数 */
        unsigned requestBeats = 1;
        /** 当前发送的拍号 */
        unsigned sent = 0;
        /** 请求传输完毕时交给目标 */
        std::function<void()> receive;
        /** 响应传输完毕时交给上游，false 表示返回反压 */
        std::function<bool()> complete;
    };
    /** 在拍 FIFO 之间共享传输记录 */
    using ItemPtr = std::shared_ptr<Item>;

    /** 构造有限槽位的链路 */
    MemoryLink(ClockedObject &owner, ClockDomain &source, ClockDomain &target,
               const std::string &name, unsigned limit);
    /** 链路可接受一个新的请求 */
    bool
    canSubmit() const
    { return used < limit; }
    /** 提交已经取得槽位的请求 */
    void submit(const ItemPtr &item);
    /** 目标已取得响应，开始返回传输 */
    void respond(const ItemPtr &item);
    /** 上游已撤销返回反压 */
    void retryResponse();
    /** 在下一个目标时钟沿处理媒体进度 */
    void wakeTarget();
    /** 数据与同步信用均已排空 */
    bool idle();
    /** 调试轨迹中的链路名 */
    const std::string &
    name() const
    { return linkName; }
    /** 进度或信用变化时通知外壳 */
    std::function<void()> onProgress;
    /** 目标时钟端的处理函数 */
    std::function<void()> onTargetProgress;
    /** 累计同步等待 tick，按拍累加 */
    uint64_t cdcTicks = 0;
    /** 累计序列化 tick，按拍累加 */
    uint64_t transferTicks = 0;
    /** 累计请求发送排队 tick */
    uint64_t queueTicks = 0;

  private:
    /** 处理源时钟端 */
    void sourceStep();
    /** 处理目标时钟端 */
    void targetStep();
    /** 调试轨迹中的链路名 */
    const std::string linkName;
    /** 事务槽位上限 */
    const unsigned limit;
    /** 已占用槽位，含返回反压 */
    unsigned used = 0;
    /** 上游正在反压响应 */
    bool responseBlocked = false;
    /** 发送方时钟 */
    MemoryClock sourceClock;
    /** 接收方时钟 */
    MemoryClock targetClock;
    /** 请求 FIFO */
    AsyncFifo<ItemPtr> requests;
    /** 响应 FIFO */
    AsyncFifo<ItemPtr> responses;
    /** 等待发送的请求 */
    std::deque<ItemPtr> outgoing;
    /** 等待返回的响应 */
    std::deque<ItemPtr> returning;
};

} // namespace gem5::gw100
#endif
