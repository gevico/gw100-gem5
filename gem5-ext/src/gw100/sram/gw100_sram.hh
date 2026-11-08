/**
 * @file  gw100_sram.hh
 * @brief SRAM timing 端口、事件与检查点外壳
 * @see spec-sram
 */
#pragma once

#include <fstream>
#include <map>
#include "base/statistics.hh"
#include "mem/port.hh"
#include "params/Gw100Sram.hh"
#include "sim/clocked_object.hh"
#include "src/gw100/sram/sram_model.hh"

namespace gem5::gw100
{
/** 为共享周期状态机提供 gem5 时钟、端口和恢复接口。 */
class Gw100Sram : public ClockedObject
{
  public:
    PARAMS(Gw100Sram);
    /** 构造实例、有限入口与可选的观测和故障夹具。 */
    explicit Gw100Sram(const Params &p);
    /** 返回硬件绑定的来源端口。 */
    Port &getPort(const std::string &name, PortID index) override;
    /** 向连接入口公布地址范围。 */
    void init() override;
    /** 收尾旧代次后开始端口清零，完成由 Ready 状态通知。 */
    void initialize();
    /** 取消未提交操作并标记入口中的旧代次请求。 */
    void resetMemory(bool preserve);
    /** 停止 SRAM 有效边沿并保存剩余流水周期。 */
    void pauseMemory();
    /** 在当前时钟域中恢复有效边沿。 */
    void resumeMemory();
    /** 检查来源的入口、阵列和返回额度是否全部释放。 */
    bool sourceDrained(unsigned source) const;
    /** 检查整个实例的入口、阵列与响应是否已排空。 */
    bool drainedMemory() const;
    /** 整体排空后进入保持，返回是否成功。 */
    bool enterRetention();
    /** 显式退出保持并恢复时钟服务。 */
    void leaveRetention();
    /** 提供组件控制与测试所需的状态机接口。 */
    SramModel &
    model()
    { return core; }
    /** 返回只读的组件状态。 */
    const SramModel &
    model() const
    { return core; }
    /** 返回软件窗口的起始地址。 */
    Addr
    base() const
    { return start; }
    /** 返回数据容量，单位为字节。 */
    uint64_t
    size() const
    { return core.config().size; }
    /** 检查全局非空请求范围，包含溢出检查。 */
    bool contains(Addr address, unsigned size) const;
    /** 请求 gem5 排空并等待全部已接受请求收尾。 */
    DrainState drain() override;
    /** 恢复排空前的正常准入。 */
    void drainResume() override;
    /** 写入稳定态组件快照与夹具状态。 */
    void serialize(CheckpointOut &cp) const override;
    /** 恢复经组织指纹校验的组件快照。 */
    void unserialize(CheckpointIn &cp) override;
    /** 将状态机计数同步到 gem5 统计。 */
    void preDumpStats() override;

  private:
    /** 单个硬件来源的 timing 响应端口。 */
    class SramPort : public ResponsePort
    {
      public:
        /** 绑定所属实例及不可伪造的来源号。 */
        SramPort(const std::string &name, Gw100Sram &owner, unsigned source);
        /** 所属共享实例。 */
        Gw100Sram &owner;
        /** 物理端口绑定的来源编号。 */
        const unsigned source;
        /** 是否欠一次请求 retry，以及返回是否被上游阻塞。 */
        bool retry = false, blocked = false;

      protected:
        /** 在有限资源允许时接受 timing 请求。 */
        bool recvTimingReq(PacketPtr packet) override;
        /** 执行显式兼容原子访问并返回参考延迟。 */
        Tick recvAtomic(PacketPtr packet) override;
        /** 对排空的存储执行调试或加载访问。 */
        void recvFunctional(PacketPtr packet) override;
        /** 上游释放响应阻塞后重新安排返回。 */
        void recvRespRetry() override;
        /** 公布该 SRAM 实例的数据窗口。 */
        AddrRangeList getAddrRanges() const override;
    };
    /** 阵列事务号到 gem5 请求对象的有限账本项。 */
    struct PacketState
    {
        /** 在响应接受前由本实例持有的请求对象。 */
        PacketPtr packet;
        /** 是否已转换为携带结果的响应。 */
        bool prepared = false;
    };
    /** 与状态机累计计数一一对应的 gem5 统计组。 */
    struct Metrics : public statistics::Group
    {
        /** 注册统计量及其字节、周期或计数单位。 */
        explicit Metrics(Gw100Sram &owner);
        /** 父事务准入、完成、拒绝，以及宏读写命令数量。 */
        statistics::Scalar accepted, completed, rejected, reads, writes;
        /** RMW、CE、UE、有效字节与初始化宏字计数。 */
        statistics::Scalar rmw, ce, ue, readBytes, writeBytes, initWrites;
        /** 累计与最长 bank 等待，以及来源队列和依赖等待。 */
        statistics::Scalar bankWait, maxBankWait, queueWait, dependencyWait;
        /** 响应就绪后累计等待接受的周期数。 */
        statistics::Scalar responseWait;
    };
    /** 独立的数据、ECC、端口和调度状态。 */
    SramModel core;
    /** 对外发布的累计统计。 */
    Metrics metrics;
    /** 数据窗口的基地址。 */
    const Addr start;
    /** 固定来源数的端口集合。 */
    std::vector<std::unique_ptr<SramPort>> ports;
    /** 已进入阵列的请求账本，数量受父槽上限约束。 */
    std::map<uint64_t, PacketState> packets;
    /** 已接受但尚未满足传输到达或阵列准入条件的请求。 */
    struct Pending
    {
        /** 请求数据及 gem5 事务身份。 */
        PacketPtr packet;
        /** 上游准入时点，单位为 tick。 */
        Tick accepted;
        /** 请求及数据传输完成时点，单位为 tick。 */
        Tick arrival;
        /** 接受时的复位代次。 */
        uint64_t epoch;
        /** 该入口请求是否已被复位退役。 */
        bool cancelled = false;
    };
    /** 每来源有界的传输等待 FIFO。 */
    std::vector<std::deque<Pending>> ingress;
    /** 包含入口、阵列及返回反压的每来源额度占用。 */
    std::vector<unsigned> inFlight;
    /** 每来源最近准入的时钟边沿。 */
    std::vector<Tick> lastAccept;
    /** 在复位与门控之后执行的 SRAM 时钟事件。 */
    EventFunctionWrapper edge;
    /** 最近处理的绝对边沿，单位为 tick。 */
    Tick lastEdge = MaxTick;
    /** gem5 排空期间关闭新准入。 */
    bool draining = false;
    /** 等待旧代次完成后开始清零。 */
    bool initializePending = false;
    /** 可选的 JSONL 事件输出。 */
    std::ofstream traceFile;
    /** 故障注入所匹配的数字事件。 */
    const std::string faultStage;
    /** 故障目标偏移与数据位翻转掩码。 */
    const uint64_t faultOffset, faultMask;
    /** 故障目标的硬件来源。 */
    const unsigned faultSource;
    /** 是否在来源的第一个匹配宏字处注入。 */
    const bool faultAnyWord;
    /** 一次性注入是否已经执行，跨 checkpoint 保存。 */
    bool faultInjected = false;
    /** 在当前或下一合法边沿安排服务事件。 */
    void arm();
    /** 执行到达准入、阵列边沿、返回和 retry。 */
    void process();
    /** 预留完整事务额度并保存带到达时间的请求。 */
    bool receive(unsigned source, PacketPtr packet);
    /** 对排空存储执行功能访问并转换为 gem5 响应。 */
    void functional(PacketPtr packet);
};
} // namespace gem5::gw100
