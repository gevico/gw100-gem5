/**
 * @file  sram_probe.hh
 * @brief 多引擎 timing 请求、背压与检查点夹具
 * @see spec-sram
 */
#pragma once
#include "params/SramProbe.hh"
#include "sim/clocked_object.hh"
#include "src/gw100/sram/gw100_sram.hh"

namespace gem5::gw100
{
/** 使用相同 timing 入口验证管理与 Compute 档案。 */
class SramProbe : public ClockedObject
{
  public:
    PARAMS(SramProbe);
    /** 创建有限来源、返回阻塞与生命周期注入事件。 */
    explicit SramProbe(const Params &p);
    /** 返回指定来源的请求端口。 */
    Port &getPort(const std::string &name, PortID index) override;
    /** 初始化新实例或继续恢复后的测试阶段。 */
    void startup() override;
    /** 在保存边界停止新流量并等待已接受操作。 */
    DrainState drain() override;
    /** checkpoint 后恢复分阶段流量。 */
    void drainResume() override;
    /** 保存测试阶段及接受、retry 计数。 */
    void serialize(CheckpointOut &cp) const override;
    /** 恢复下一阶段所需的夹具状态。 */
    void unserialize(CheckpointIn &cp) override;

  private:
    /** 单个有界引擎来源与可控返回阻塞。 */
    class ProbePort : public RequestPort
    {
      public:
        /** 绑定固定来源及所属夹具。 */
        ProbePort(const std::string &name, SramProbe &probe, unsigned source)
            : RequestPort(name), probe(probe), source(source)
        {}
        /** 所属夹具。 */
        SramProbe &probe;
        /** 引擎来源号。 */
        unsigned source;
        /** 唯一尚未获准的请求。 */
        PacketPtr pending = nullptr;
        /** 已接受未返回的数量，以及本阶段已发出数量。 */
        unsigned outstanding = 0, issued = 0;
        /** 请求等待 retry、返回阻塞、是否已注入首次阻塞。 */
        bool waiting = false, blocked = false, stalled = false;
        /** 返回阻塞的释放 tick。 */
        Tick release = 0;

      protected:
        /** 注入一次返回反压，其余响应交给数据检查。 */
        bool recvTimingResp(PacketPtr packet) override;
        /** 记录请求可以重新准入。 */
        void
        recvReqRetry() override
        { waiting = false; }
    };
    /** 每笔请求的独立数据期望索引。 */
    struct Tag : public Packet::SenderState
    {
        /** 请求索引及发出时所属阶段。 */
        unsigned index, phase;
        /** 保存来源内请求索引与阶段。 */
        Tag(unsigned index, unsigned phase) : index(index), phase(phase) {}
    };
    /** 被测共享 SRAM 实例。 */
    Gw100Sram &sram;
    /** 固定数量的硬件来源。 */
    std::vector<std::unique_ptr<ProbePort>> ports;
    /** 分阶段流量的时钟事件。 */
    EventFunctionWrapper edge;
    /** 复位优先、暂停和切换时钟后的恢复事件。 */
    EventFunctionWrapper resetEdge, pauseEdge, resumeEdge;
    /** 当前测试阶段，0 等待初始化，7 为保存与恢复边界。 */
    unsigned phase = 0;
    /** 总准入数量与请求 retry 数量。 */
    uint64_t accepted = 0, retries = 0;
    /** 是否正在响应 gem5 排空。 */
    bool draining = false;
    /** 是否保存快照，以及快照是否处于保持状态。 */
    const bool snapshot, retention;
    /** 首次响应暂停的夹具周期数。 */
    const unsigned stallCycles;
    /** 是否启用提交边沿复位或启动时钟切换。 */
    const bool resetTest, clockSwitch;
    /** 两类生命周期事件是否已经注入。 */
    bool resetInjected = false, switchStarted = false;
    /** 每来源普通读写阶段的请求数量。 */
    static constexpr unsigned Count = 32;
    /** 逐周期推进流量、返回释放及测试阶段。 */
    void process();
    /** 为一个来源发出或重试唯一待发请求。 */
    void send(ProbePort &port);
    /** 独立核对错误状态及每个读取字节。 */
    void received(ProbePort &port, PacketPtr packet);
    /** 生成来源及索引对应的全局 SRAM 地址。 */
    uint64_t address(unsigned source, unsigned index) const;
    /** 独立生成初始化和请求模式对应的数据期望。 */
    uint8_t expected(unsigned source, unsigned index, unsigned byte) const;
};
} // namespace gem5::gw100
