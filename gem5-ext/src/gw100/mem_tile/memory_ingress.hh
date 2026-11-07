/**
 * @file  memory_ingress.hh
 * @brief 可信来源的 timing 接入与事务拆分
 *
 * @see spec-dram, spec-address-map
 */

#ifndef GW100_MEMORY_INGRESS_HH
#define GW100_MEMORY_INGRESS_HH

#include <array>
#include <deque>
#include <memory>
#include <vector>

#include "mem/port.hh"
#include "params/Gw100MemoryIngress.hh"
#include "src/gw100/mem_tile/generated/memory_defs.hh"
#include "src/gw100/mem_tile/memory_link.hh"

namespace gem5::gw100
{

class Gw100MemoryTile;

/** 一个硬件来源的有界接入端 */
class Gw100MemoryIngress : public ClockedObject
{
  public:
    PARAMS(Gw100MemoryIngress);
    /** 绑定时钟、设备 System 和来源 */
    explicit Gw100MemoryIngress(const Params &p);
    /** 查找上下游端口 */
    Port &getPort(const std::string &name,
                  PortID idx = InvalidPortID) override;
    /** 发布地址范围 */
    void init() override;
    /** 上游已停止发请求后排空链路 */
    DrainState drain() override;
    /** 保存空链路的来源代次和计数器 */
    void serialize(CheckpointOut &cp) const override;
    /** 恢复空链路的来源代次和计数器 */
    void unserialize(CheckpointIn &cp) override;
    /** CRC 释放或复位连接路径 */
    void setEnabled(bool value);
    /** FLR 或电源切换作废来源旧事务 */
    void invalidate();
    /** 来源事务和同步信用均已排空 */
    bool idle();
    /** 指定全局通道仍有已准入的数据事务 */
    bool dataPending(unsigned channel) const;
    /** 读取请求、字节、retry、CDC、排队、传输的累计值 */
    uint64_t metric(unsigned index) const;

  private:
    /** 上游请求端口 */
    class FrontPort : public ResponsePort
    {
      public:
        /** 绑定所属入口 */
        FrontPort(const std::string &name, Gw100MemoryIngress &owner);
        /** 当前实现的数据和控制范围 */
        AddrRangeList getAddrRanges() const override;
        /** 接受或反压请求 */
        bool recvTimingReq(PacketPtr pkt) override;
        /** 精确配置拒绝 atomic 路径 */
        Tick recvAtomic(PacketPtr pkt) override;
        /** 调试只读控制寄存器 */
        void recvFunctional(PacketPtr pkt) override;
        /** 上游撤销响应反压 */
        void recvRespRetry() override;

      private:
        /** 所属入口 */
        Gw100MemoryIngress &owner;
    };
    /** 原始请求的完成状态 */
    struct Context
    {
        /** 原始请求占用的 CDC 槽位 */
        MemoryLink::ItemPtr item;
        /** 已完成字节 */
        unsigned offset = 0;
        /** 出错后停止后续子请求 */
        bool error = false;
        /** 所属来源代次 */
        uint64_t generation = 0;
        /** 事务编号 */
        uint64_t transaction = 0;
        /** 整个原始数据事务占用的通道位图 */
        unsigned channels = 0;
    };
    /** 子请求上的来源和原始请求引用 */
    struct ForwardState : public MemorySourceState
    {
        /** 原始请求 */
        std::shared_ptr<Context> context;
        /** 子请求字节数 */
        unsigned size = 0;
    };
    /** 连接临时核心互连的请求端口 */
    class BackPort : public RequestPort
    {
      public:
        /** 绑定所属入口 */
        BackPort(const std::string &name, Gw100MemoryIngress &owner);
        /** 排队并尝试发送一个有界子请求 */
        void send(PacketPtr pkt);
        /** 没有等待接收的子请求 */
        bool
        idle() const
        { return waiting.empty(); }

      protected:
        /** 接受返回包 */
        bool recvTimingResp(PacketPtr pkt) override;
        /** 下游撤销请求反压 */
        void recvReqRetry() override;

      private:
        /** 所属入口 */
        Gw100MemoryIngress &owner;
        /** 有界事务的待发送子请求 */
        std::deque<PacketPtr> waiting;
    };
    /** 接受一个完整请求 */
    bool receive(PacketPtr pkt);
    /** 完整请求预检后开始发送 */
    void forward(const std::shared_ptr<Context> &ctx);
    /** 返回一个子请求 */
    void returned(PacketPtr pkt);
    /** 在互连返回延迟结束后处理子请求 */
    void completeReturn(PacketPtr pkt);
    /** 推进有界的互连返回队列 */
    void processReturns();
    /** 完成或拒绝一个原始请求 */
    void finish(const std::shared_ptr<Context> &ctx);
    /** 通知 retry、排空和关联 Tile */
    void progress();

    /** 入口来源编号 */
    const unsigned source;
    /** 设备 System 内的请求者编号 */
    const RequestorID requestor;
    /** Memory Tile 列表 */
    const std::vector<Gw100MemoryTile *> tiles;
    /** 上游响应端口 */
    FrontPort front;
    /** 下游请求端口 */
    BackPort back;
    /** 来源到核心的双向 CDC */
    MemoryLink link;
    /** CRC 已释放临时连接 */
    bool enabled = false;
    /** 来源复位代次 */
    uint64_t generation = 0;
    /** 下一事务编号 */
    uint64_t nextTransaction = 1;
    /** 上游需要重试通知 */
    bool requestRetry = false;
    /** 各通道已准入且尚未完成上游返回的数据事务数 */
    std::array<unsigned, memorydefs::CHANNELS> pendingData{};
    /** 请求、读字节、写字节与 retry 计数 */
    std::array<uint64_t, 4> counters{};
    /** 带返回到达时刻的子响应，受来源槽位约束 */
    std::deque<std::pair<Tick, PacketPtr>> returns;
    /** 返回路径到达事件 */
    EventFunctionWrapper returnEvent;
};

} // namespace gem5::gw100
#endif
