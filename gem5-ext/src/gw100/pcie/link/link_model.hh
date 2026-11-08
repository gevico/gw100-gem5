/**
 * @file  link_model.hh
 * @brief 双向 PCIe 数字链路与有界事务层接口
 *
 * 链路内核独立于 gem5 Packet。端口适配器和独立夹具共用该接口，
 * 目标服务与请求者账本拥有各自的实体资源。
 *
 * @see spec-pcie
 */

#ifndef GW100_PCIE_LINK_MODEL_HH
#define GW100_PCIE_LINK_MODEL_HH

#include <array>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "src/gw100/pcie/link/flow_control.hh"
#include "src/gw100/pcie/link/generated/link_defs.hh"
#include "src/gw100/pcie/link/packet.hh"
#include "src/gw100/pcie/link/replay.hh"
#include "src/gw100/pcie/link/serializer.hh"

namespace gem5::gw100::pcie
{

/** 状态机使用的 1 ps 时钟和单个唤醒事件。 */
class LinkClockIf
{
  public:
    /** 析构。 */
    virtual ~LinkClockIf() = default;
    /** 当前时刻，单位 ps。 */
    virtual uint64_t now() const = 0;
    /** 在不早于指定时刻调用 LinkModel::tick()。 */
    virtual void wakeAt(uint64_t when_ps) = 0;
};

/** 数字训练和低功耗的可观测状态。 */
enum class LinkState : unsigned
{
    /** PERST 或 Link Down。 */
    Down,
    /** Detect.Quiet。 */
    DetectQuiet,
    /** Detect.Active。 */
    DetectActive,
    /** 部分 lane 检测后复查。 */
    DetectRecheck,
    /** Polling.Active。 */
    PollingActive,
    /** Polling.Configuration。 */
    PollingConfiguration,
    /** Configuration.Linkwidth.Start。 */
    ConfigWidthStart,
    /** Configuration.Linkwidth.Accept。 */
    ConfigWidthAccept,
    /** Configuration.Lanenum.Wait。 */
    ConfigLaneWait,
    /** Configuration.Lanenum.Accept。 */
    ConfigLaneAccept,
    /** Configuration.Complete。 */
    ConfigComplete,
    /** Configuration.Idle。 */
    ConfigIdle,
    /** 正常串行传输，Data Link 初始化另有独立状态。 */
    L0,
    /** Recovery.RcvrLock。 */
    RecoveryLock,
    /** Recovery.RcvrCfg。 */
    RecoveryConfiguration,
    /** Recovery.Speed。 */
    RecoverySpeed,
    /** Recovery.Equalization Phase 0。 */
    Equalization0,
    /** Recovery.Equalization Phase 1。 */
    Equalization1,
    /** Recovery.Equalization Phase 2。 */
    Equalization2,
    /** Recovery.Equalization Phase 3。 */
    Equalization3,
    /** Recovery.Idle。 */
    RecoveryIdle,
    /** ASPM 或 PCI-PM L1 协商。 */
    L1Negotiation,
    /** 已发送 EIOS，等待最小电气空闲时间。 */
    L1Entry,
    /** L1.Idle。 */
    L1,
    /** Hot Reset TS 握手和接收端保持。 */
    HotReset,
    /** 软件关闭链路。 */
    Disabled,
};

/** 观测事件类别。 */
enum class LinkPoint : unsigned
{
    /** 本地事务层接受。 */
    Accept,
    /** 首次进入数据链路层。 */
    Commit,
    /** TLP 首个符号发送。 */
    Transmit,
    /** TLP 包尾到达对端。 */
    Receive,
    /** 累计 ACK 返回并释放 replay。 */
    Acknowledge,
    /** 目标操作可见。 */
    Visible,
    /** 请求者收到并验证完整完成。 */
    Complete,
    /** DLLP 发送。 */
    DllpTransmit,
    /** DLLP 接收。 */
    DllpReceive,
    /** SKP 及必要 framing。 */
    Skip,
    /** 训练、SDS、EIOS 或 EIEOS。 */
    OrderedSet,
    /** 状态变化。 */
    State,
    /** 错误。 */
    Error,
    /** 事务取消。 */
    Cancel,
    /** 全部事务与 RX 释放结算后的排空观测。 */
    Drained,
    /** 原生目标开始服务。 */
    TargetBegin,
    /** 原生目标结果可见或可用于构造完成。 */
    TargetEnd,
    /** 本地超时或隔离结束，停止等待线上完成。 */
    Retired,
};

/** 链路检测到的错误类型。 */
enum class LinkError : unsigned
{
    /** Bad TLP。 */
    BadTlp,
    /** 数字 PHY 或 framing 的 Receiver Error。 */
    Receiver,
    /** Bad DLLP。 */
    BadDllp,
    /** Replay Timer Timeout。 */
    ReplayTimeout,
    /** Replay Number Rollover。 */
    ReplayRollover,
    /** Data Link Protocol Error。 */
    DataLinkProtocol,
    /** Flow Control Protocol Error。 */
    FlowControl,
    /** Receiver Overflow。 */
    ReceiverOverflow,
    /** Malformed TLP。 */
    Malformed,
    /** Surprise Down。 */
    SurpriseDown,
    /** 数字 PHY 未满足该训练阶段，独立诊断计数。 */
    TrainingTimeout,
    /** 请求无法由目标支持。 */
    UnsupportedRequest,
    /** 目标主动终止完成。 */
    CompleterAbort,
    /** 收到 poisoned TLP。 */
    Poisoned,
    /** 完成没有可匹配的请求。 */
    UnexpectedCompletion,
    /** 请求者等待 completion 超时。 */
    CompletionTimeout,
};

/** 数字故障反馈，作用于后续匹配的发送包。 */
enum class LinkFault : unsigned
{
    /** 破坏 TLP LCRC。 */
    TlpCrc,
    /** 破坏 DLLP CRC。 */
    DllpCrc,
    /** 丢失 TLP。 */
    DropTlp,
    /** 丢失累计 ACK。 */
    DropAck,
    /** 丢失累计 FC 更新。 */
    DropUpdate,
    /** 破坏 Gen3/4 STP Frame CRC。 */
    Framing,
    /** 强制再次发送相同序号的保留副本。 */
    Duplicate,
    /** 可注入类型数。 */
    Count,
};

/** 独立参考与端口适配器共用的观测记录。 */
struct LinkTrace
{
    /** 观测事件。 */
    LinkPoint point = LinkPoint::State;
    /** 事件时刻，单位 ps。 */
    uint64_t tick = 0;
    /** 本事件所属侧，RC 为 0，EP 为 1。 */
    unsigned side = 0;
    /** 本地 TLP 观测编号。 */
    uint64_t cookie = 0;
    /** 线上序号或 DLLP Type。 */
    unsigned value = 0;
    /** 是否为重传。 */
    bool replay = false;
    /** 此次串行占用的精确相位。 */
    FrameTime frame;
    /** 可选的原始数据链路包或 DLLP。 */
    std::vector<uint8_t> bytes;
    /** Gen3/4 物理 STP Token，Gen1/2 和其他事件为空。 */
    std::vector<uint8_t> framing;
};

/** 参考设计、数字 PHY 反馈与故障设置。 */
struct LinkConfig
{
    /** 目标代际。 */
    unsigned generation = pciedefs::DEFAULT_GENERATION;
    /** 目标宽度。 */
    unsigned width = pciedefs::DEFAULT_WIDTH;
    /** 数字 PHY 实际能达到的最大代际。 */
    unsigned phyGeneration = 4;
    /** 数字 PHY 检测到的 lane，首版物理端口为四 lane。 */
    unsigned phyLaneMask = 15;
    /** 数字 PHY 每次反馈的等待，单位 ps。 */
    uint64_t phyFeedbackPs = pciedefs::PHY_FEEDBACK_PS;
    /** 数字 PHY 每次均衡评估等待，单位 ps。 */
    uint64_t equalizationPs = pciedefs::PHY_EQ_EVALUATION_PS;
    /** 单向传播，单位 ps。 */
    uint64_t propagationPs = pciedefs::ONE_WAY_PROPAGATION_PS;
    /** 两侧控制器周期，单位 ps。 */
    uint64_t controllerPs = pciedefs::CONTROLLER_PERIOD_PS;
    /** 指定训练阶段不返回成功，Down 表示正常反馈。 */
    LinkState failTraining = LinkState::Down;
};

/** 每侧累计统计，时间均以 ps 或明确的半皮秒计量。 */
struct LinkCounters
{
    /** 首次发送的 TLP 数。 */
    uint64_t tlpTransmit = 0;
    /** 成功交给事务层的 TLP 数。 */
    uint64_t tlpReceive = 0;
    /** 首次发送的有效数据载荷，单位 B。 */
    uint64_t payloadTransmit = 0;
    /** 成功接收的数据载荷，单位 B。 */
    uint64_t payloadReceive = 0;
    /** DLLP 发送数。 */
    uint64_t dllpTransmit = 0;
    /** DLLP 接收数。 */
    uint64_t dllpReceive = 0;
    /** 重传 TLP 数。 */
    uint64_t replayTransmit = 0;
    /** 丢弃的重复 TLP 数。 */
    uint64_t duplicateReceive = 0;
    /** TLP/DLLP CRC 错误数。 */
    uint64_t crcErrors = 0;
    /** 信用不足的仲裁检查数。 */
    uint64_t creditStalls = 0;
    /** replay 资源不足的仲裁检查数。 */
    uint64_t replayStalls = 0;
    /** TLP 在本地待发队列的累计等待，单位 ps。 */
    uint64_t queueWaitPs = 0;
    /** 编码包串行占用，单位 0.5 ps。 */
    uint64_t packetHalf = 0;
    /** SKP 和其必要 framing 占用，单位 0.5 ps。 */
    uint64_t maintenanceHalf = 0;
    /** 训练和链路状态 Ordered Set 占用，单位 0.5 ps。 */
    uint64_t trainingHalf = 0;
};

/** 用于独立守恒检查的当前实体资源占用。 */
struct LinkResources
{
    /** 三类待发头部数。 */
    std::array<unsigned, 3> txHeaders{};
    /** 三类待发数据字节。 */
    std::array<unsigned, 3> txBytes{};
    /** 三类仍占有 RX 存储的头部数。 */
    std::array<unsigned, 3> rxHeaders{};
    /** 三类仍占有 RX 存储的数据字节。 */
    std::array<unsigned, 3> rxBytes{};
    /** 当前 replay 数据字节。 */
    unsigned replayBytes = 0;
    /** 当前 replay 描述符数。 */
    unsigned replayHeaders = 0;
};

/** 已排空端口的可恢复状态。 */
struct LinkPortSnapshot
{
    /** 串行相位。 */
    Serializer::State wire;
    /** 三类累计头部信用。 */
    std::array<CreditCounter::State, 3> headers;
    /** 三类累计数据信用。 */
    std::array<CreditCounter::State, 3> data;
    /** 三类已发放累计头部信用。 */
    std::array<uint32_t, 3> allocatedHeaders{};
    /** 三类已发放累计数据信用。 */
    std::array<uint32_t, 3> allocatedData{};
    /** 下一发送序号。 */
    uint16_t nextTransmit = 0;
    /** 最近确认序号。 */
    uint16_t acknowledged = 4095;
    /** 下一预期接收序号。 */
    uint16_t nextReceive = 0;
    /** 暂停时 FC 期限剩余值，单位 ps。 */
    uint64_t flowRemaining = UINT64_MAX;
    /** 有效 MPS，单位 B。 */
    unsigned mps = 128;
    /** Extended Synch。 */
    bool extendedSynch = false;
    /** ASPM L1 使能。 */
    bool aspm = false;
    /** 各类轮询下次优先项。 */
    unsigned roundRobin = 0;
    /** 可控故障剩余次数。 */
    std::array<unsigned, unsigned(LinkFault::Count)> faults{};
    /** 累计统计。 */
    LinkCounters stats;
};

/** 仅允许在已排空 L0/L1 状态保存的链路快照。 */
struct LinkSnapshot
{
    /** 快照格式版本。 */
    unsigned version = 1;
    /** 暂停时刻，单位 ps。 */
    uint64_t tick = 0;
    /** L0 或 L1。 */
    LinkState state = LinkState::L0;
    /** 实际速率。 */
    unsigned generation = 1;
    /** 实际宽度。 */
    unsigned width = 4;
    /** 已完成均衡的代际位图。 */
    unsigned equalized = 0;
    /** 下一个观测编号。 */
    uint64_t nextCookie = 1;
    /** 下一个队列顺序号。 */
    uint64_t nextOrder = 0;
    /** EP 电源状态。 */
    bool d3hot = false;
    /** 最近业务活动与暂停时刻的间隔，单位 ps。 */
    uint64_t idleElapsed = 0;
    /** L1 拒绝后的剩余等待，单位 ps。 */
    uint64_t powerRetryRemaining = 0;
    /** 双端排空状态。 */
    std::array<LinkPortSnapshot, 2> ports;
};

/** 目标接收与请求者事件接口。 */
class LinkPeerIf
{
  public:
    /** 析构。 */
    virtual ~LinkPeerIf() = default;
    /**
     * 目标能保留完整操作时返回 true。释放 RX 存储时调用 release，
     * 目标继续使用自己的有限执行槽。返回 false 时保留 RX 并等待重试。
     */
    virtual bool receive(unsigned side, const Tlp &tlp,
                         std::function<void()> release) = 0;
    /** 源端 TLP 已进入数据链路层。 */
    virtual void
    committed(unsigned, uint64_t)
    {}
    /** replay 副本已被累计确认。 */
    virtual void
    acknowledged(unsigned, uint64_t)
    {}
    /** 发送资源变化，可重试本地准入。 */
    virtual void
    transmitReady(unsigned)
    {}
    /** 物理复位作废本地事务，submitted 指该 TLP 曾经进入 DLL。 */
    virtual void
    cancelled(unsigned, uint64_t, bool)
    {}
    /** 错误诊断，header 可为空。 */
    virtual void
    error(unsigned, LinkError, const std::vector<uint8_t> &)
    {}
    /** 链路状态、速率或 DLL 就绪状态变化。 */
    virtual void
    stateChanged()
    {}
    /** 请求者账本与目标后端均已排空。 */
    virtual bool
    quiescent() const
    {
        return true;
    }
    /** 可选的事件级观测。 */
    virtual void
    observe(const LinkTrace &)
    {}
    /** 排空完成时通知所属 SimObject。 */
    virtual void
    drained()
    {}
};

/** 双向共享的数字链路内核。 */
class LinkModel
{
  public:
    /** 构造后由 start() 执行真实数字训练。 */
    LinkModel(LinkClockIf &clock, LinkPeerIf &peer, const LinkConfig &config);
    /** 从 Detect 开始建立链路。 */
    void start();
    /** 运行当前时刻的事件、协议期限和仲裁。 */
    void tick();
    /** 只查询本地准入，尚未初始化的数据链路返回 false。 */
    bool canSubmit(unsigned side, const Tlp &tlp) const;
    /** 接受并复制完整 TLP，成功时为 cookie 为零的请求分配观测编号。 */
    bool submit(unsigned side, Tlp &tlp);
    /** 只撤销尚未进入 DLL 的本地 TLP，已提交时返回 false。 */
    bool cancel(unsigned side, uint64_t cookie);
    /** 目标的有限后端可以再次接受，解除接收重试。 */
    void retryReceive();
    /** 请求者记录线上完成已全部校验。 */
    void noteComplete(unsigned side, uint64_t cookie);
    /** 本地超时或隔离结束，停止等待线上完成。 */
    void noteRetired(unsigned side, uint64_t cookie);
    /** 将事务层检测的错误计入共同 trace 和 AER 回调。 */
    void noteError(unsigned side, LinkError error,
                   const std::vector<uint8_t> &header);
    /** 目标记录操作或写入已可见。 */
    void noteVisible(unsigned side, uint64_t cookie);
    /** 改变后续构包的 MPS 和 Extended Synch，缩减前需排空。 */
    bool configure(unsigned side, unsigned mps, bool extended_synch);
    /** 软件为指定侧使能或关闭 ASPM L1。 */
    void setAspm(unsigned side, bool enabled);
    /** EP 的 D3hot 状态触发 PCI-PM L1，D0 请求恢复。 */
    void setD3hot(bool enabled);
    /** 立即门控新提交，PERST 撤销后重新训练。 */
    void setPerst(bool asserted);
    /** 在正常链路上发起 Hot Reset 握手。 */
    void hotReset();
    /** 物理链路丢失，作废链路代次并重新执行 Detect。 */
    void linkDown();
    /** 请求保留序号和信用的 Recovery，可改变目标速率或宽度。 */
    void retrain(unsigned generation, unsigned width);
    /** 在指定发送方向注入有限次数的数字故障。 */
    void inject(unsigned side, LinkFault fault, unsigned count = 1);
    /** 停止维护流量前先排空全部已接受事务。 */
    void drain();
    /** 排空恢复后继续维护流量和正常准入。 */
    void resume();
    /** 当前 PHY/LTSSM 状态。 */
    LinkState
    state() const
    {
        return linkState;
    }
    /** 当前协商的实际代际。 */
    unsigned
    generation() const
    {
        return actualGeneration;
    }
    /** 当前协商的实际宽度。 */
    unsigned
    width() const
    {
        return actualWidth;
    }
    /** 指定侧的 VC0 已完成 Data Link 初始化。 */
    bool active(unsigned side) const;
    /** 子系统已排空并冻结维护事件。 */
    bool
    isDrained() const
    {
        return frozen;
    }
    /** 指定侧累计统计。 */
    const LinkCounters &counters(unsigned side) const;
    /** 指定侧当前 replay 数据存储，单位 B。 */
    unsigned replayBytes(unsigned side) const;
    /** 指定侧当前待发头部数。 */
    unsigned queued(unsigned side, Traffic traffic) const;
    /** 读取实体资源占用，供适配器和独立检查器核算。 */
    LinkResources resources(unsigned side) const;
    /** 保存已排空的 L0/L1 状态，其他状态明确报错。 */
    LinkSnapshot snapshot() const;
    /** 恢复到排空状态，由 resume() 继续协议维护。 */
    void restore(const LinkSnapshot &snapshot);

  private:
    /** 无期限值，单位 ps。 */
    static constexpr uint64_t Never = UINT64_MAX;
    /** VC0 的数据链路初始化阶段。 */
    enum class FcPhase
    {
        /** 交换 Scaled FC 支持位。 */
        Feature,
        /** 收集三类初始广告。 */
        Init1,
        /** 确认对端初始化。 */
        Init2,
        /** VC0 可发送 TLP。 */
        Active,
    };
    /** 等待数据链路准入的完整 TLP。 */
    struct Queued
    {
        /** 请求和独立载荷副本。 */
        Tlp tlp;
        /** 本地接受时刻，单位 ps。 */
        uint64_t accepted = 0;
        /** 两周期适配流水结束时刻，单位 ps。 */
        uint64_t ready = 0;
        /** 同一方向的接受顺序，独立于相同时刻和外部 cookie。 */
        uint64_t order = 0;
    };
    /** 事务层 RX 存储，生命周期长于目标准入。 */
    struct Received
    {
        /** 接收包。 */
        Tlp tlp;
        /** 所属物理链路代次。 */
        uint64_t epoch = 0;
        /** 两周期接收适配流水结束，单位 ps。 */
        uint64_t ready = 0;
        /** 是否已经交给目标后端。 */
        bool delivered = false;
        /** RX 实体存储已释放。 */
        bool released = false;
    };
    /** 一个侧的发送、接收和 DLL 状态。 */
    struct Port
    {
        /** 按共享档案创建有限资源。 */
        Port();
        /** 该侧串行发送器。 */
        Serializer wire{1, 4};
        /** 原始发送副本。 */
        ReplayWindow replay;
        /** 三类待发事务。 */
        std::array<std::deque<Queued>, 3> transmit;
        /** 各类待发数据分配量，单位 B。 */
        std::array<unsigned, 3> transmitBytes{};
        /** 有限待发 DLLP，按类型合并累计值。 */
        std::deque<Dllp> dllps;
        /** 待重放的原始序号。 */
        std::deque<uint16_t> replays;
        /** 接收端仍持有的实体缓冲。 */
        std::deque<std::shared_ptr<Received>> receive;
        /** 三类头部累计发送信用。 */
        std::array<CreditCounter, 3> headers{
            CreditCounter(8), CreditCounter(8), CreditCounter(8)};
        /** 三类数据累计发送信用。 */
        std::array<CreditCounter, 3> data{CreditCounter(12), CreditCounter(12),
                                          CreditCounter(12)};
        /** 当前三类接收头部占用。 */
        std::array<unsigned, 3> receiveHeaders{};
        /** 当前三类接收存储占用，单位 B。 */
        std::array<unsigned, 3> receiveBytes{};
        /** 本侧累计发放的头部信用。 */
        std::array<uint32_t, 3> allocatedHeaders{};
        /** 本侧累计发放的数据信用。 */
        std::array<uint32_t, 3> allocatedData{};
        /** VC0 初始化阶段。 */
        FcPhase phase = FcPhase::Feature;
        /** 已收到三类初始预算的位图。 */
        unsigned initialMask = 0;
        /** 收到对端 Data Link Feature。 */
        bool remoteFeature = false;
        /** 对端确认了本侧 Data Link Feature。 */
        bool remoteFeatureAck = false;
        /** 当前发送方向的包尾，单位 ps。 */
        uint64_t busyUntil = 0;
        /** 下一个接收 TLP 的预期序号。 */
        uint16_t nextReceive = 0;
        /** NAK 已安排，收到正确的预期包后清零。 */
        bool nakScheduled = false;
        /** 上次安排 ACK 后尚未再次确认的接收包数。 */
        unsigned ackCount = 0;
        /** 累计 ACK 内部安排时刻，单位 ps。 */
        uint64_t ackDue = Never;
        /** 重放期限，单位 ps。 */
        uint64_t replayDue = Never;
        /** Recovery 期间冻结的剩余重放时间，单位 ps。 */
        uint64_t replayRemaining = Never;
        /** Recovery/L1 期间冻结的 FC 剩余时间，单位 ps。 */
        uint64_t flowRemaining = Never;
        /** 两位重放次数。 */
        unsigned replayRounds = 0;
        /** 周期 FC 或初始化重发时刻，单位 ps。 */
        uint64_t flowDue = Never;
        /** 轮询仲裁下次优先类别。 */
        unsigned roundRobin = 0;
        /** 有效 MPS，单位 B。 */
        unsigned mps = 128;
        /** Extended Synch。 */
        bool extendedSynch = false;
        /** 软件使能 ASPM L1。 */
        bool aspm = false;
        /** 已停止从事务层提交 TLP。 */
        bool blockTlp = false;
        /** 该方向已发送 L1 EIOS。 */
        bool idleSent = false;
        /** 数字故障剩余次数。 */
        std::array<unsigned, unsigned(LinkFault::Count)> faults{};
        /** 累计统计。 */
        LinkCounters stats;
    };

    /** 下一控制器沿加适配流水，单位 ps。 */
    uint64_t adapterReady(uint64_t when) const;
    /** 某类待发或接收头部实体容量。 */
    unsigned headerCapacity(Traffic traffic, bool receive) const;
    /** 某类待发或接收数据实体容量，单位 B。 */
    unsigned dataCapacity(Traffic traffic, bool receive) const;
    /** 本地实体缓冲对载荷的分配量，单位 B。 */
    unsigned dataAllocation(const Tlp &tlp) const;
    /** 所选 ACK 最大延迟，单位 ps。 */
    uint64_t ackLatency(const Port &port) const;
    /** 所选 replay 期限，单位 ps。 */
    uint64_t replayLatency(const Port &port) const;
    /** 在物理链路代次内安排一次传输或 RX 释放事件。 */
    void at(uint64_t when, std::function<void()> action);
    /** 记录观测事件。 */
    void emit(LinkPoint point, unsigned side, uint64_t cookie = 0,
              unsigned value = 0, const FrameTime &frame = {},
              const std::vector<uint8_t> &bytes = {}, bool replay = false,
              const std::vector<uint8_t> &framing = {});
    /** 记录错误并通知所属端口。 */
    void report(unsigned side, LinkError error,
                const std::vector<uint8_t> &header = {});
    /** 改变当前可观测链路状态。 */
    void setState(LinkState state);
    /** 计算下次需要唤醒的事件和协议期限。 */
    void arm();
    /** 执行下一训练阶段或其规定失败路径。 */
    void advanceTraining();
    /** 发送足够的 TS 并在相应数字反馈检查点等待。 */
    void trainStep(LinkState state, unsigned minimum_tx,
                   unsigned receive_count, unsigned after_first,
                   uint64_t timeout_ps);
    /** 进入 Configuration.Idle 或 Recovery.Idle 并发送 SDS 与 IDL。 */
    void enterIdle(bool recovery);
    /** 开始保留数据链路状态的 Recovery。 */
    void beginRecovery();
    /** L0 到达后启动或恢复 DLL。 */
    void enterL0();
    /** 对指定侧安排 Feature、InitFC1 或 InitFC2。 */
    void sendInitialization(unsigned side);
    /** 加入有界 DLLP 队列，累计更新按类型合并。 */
    void queueDllp(unsigned side, const Dllp &dllp);
    /** 在该侧形成最新累计 FC 广告。 */
    Dllp creditAdvertisement(unsigned side, unsigned traffic,
                             uint8_t base_type) const;
    /** 处理接收的有效 DLLP。 */
    void receiveDllp(unsigned side, const std::vector<uint8_t> &bytes);
    /** 安排累计 ACK 或 NAK。 */
    void acknowledgeReceive(unsigned side, bool nak, bool immediate);
    /** 处理收到的 TLP、完整性与重复抑制。 */
    void receiveTlp(unsigned side, uint64_t cookie,
                    const std::vector<uint8_t> &bytes, bool framing_error);
    /** 将有限 RX 缓冲按顺序交给可接收的目标。 */
    void deliverReceive(unsigned side);
    /** RX 处理者释放一次实体缓冲并安排累计信用归还。 */
    void releaseReceive(unsigned side,
                        const std::shared_ptr<Received> &received);
    /** 一个方向在当前边界选择 DLLP、SKP、replay 或新 TLP。 */
    void transmit(unsigned side);
    /** 从有界待发队列选取符合排序与资源条件的 TLP。 */
    int selectTraffic(unsigned side);
    /** 发送一个 DLLP，保留独立的反向传播。 */
    void transmitDllp(unsigned side, const Dllp &dllp);
    /** 发送一个原始或重放的不可变副本。 */
    void transmitTlp(unsigned side,
                     const std::shared_ptr<const ReplayWindow::Entry> &entry,
                     bool replay);
    /** 启动一次保留原包、信用和序号的重放。 */
    void beginReplay(unsigned side, bool timeout);
    /** 消耗指定的单次数字故障。 */
    bool takeFault(unsigned side, LinkFault fault);
    /** 当前是否可进入 L1 协商。 */
    bool canEnterL1() const;
    /** 启动 EP 发起的 ASPM 或 PCI-PM L1 协商。 */
    void beginL1();
    /** 推进 PM DLLP、EIOS 和 L1 最短停留要求。 */
    void progressPower();
    /** 请求恢复 L1，协商中则先完成进入。 */
    void wakeLink();
    /** 所有本地缓冲、重放和传输事件均已结算。 */
    bool empty() const;
    /** 物理链路复位，旧代次传输和回调失效。 */
    void resetPhysical(bool restart);

    /** 外部时钟。 */
    LinkClockIf &clock;
    /** 目标、请求者和观测接口。 */
    LinkPeerIf &peer;
    /** 本次参考配置。 */
    LinkConfig config;
    /** RC 与 EP 的独立方向和 DLL 状态。 */
    std::array<Port, 2> ports;
    /** 已安排的有限传输与释放事件，按时刻和插入顺序处理。 */
    std::multimap<uint64_t, std::function<void()>> events;
    /** 当前数字链路状态。 */
    LinkState linkState = LinkState::Down;
    /** 当前实际代际。 */
    unsigned actualGeneration = 1;
    /** 当前协商宽度。 */
    unsigned actualWidth = 4;
    /** 本轮 Recovery 的目标代际。 */
    unsigned recoveryGeneration = 1;
    /** 本轮 Recovery 的目标宽度。 */
    unsigned recoveryWidth = 4;
    /** 当前物理链路代次。 */
    uint64_t epoch = 0;
    /** 下次本地观测编号。 */
    uint64_t nextCookie = 1;
    /** 下一次队列接受顺序。 */
    uint64_t nextOrder = 0;
    /** 当前训练或 L1 等待期限，单位 ps。 */
    uint64_t stateDue = Never;
    /** 当前训练步骤未得到成功反馈。 */
    bool trainingFailure = false;
    /** 当前均衡完成的代际位图。 */
    unsigned equalized = 0;
    /** 当前是否在执行速率或宽度切换。 */
    bool changingSpeed = false;
    /** Recovery 完成后需重新协商 lane 号。 */
    bool changingWidth = false;
    /** Detect.Active 中记录的检测位图。 */
    unsigned detectionMask = 0;
    /** 等待当前包完成后进入 Recovery。 */
    bool recoveryRequested = false;
    /** PERST 仍保持有效。 */
    bool perst = false;
    /** EP 处于 D3hot。 */
    bool d3hot = false;
    /** L1 协商属于 PCI-PM。 */
    bool pciPower = false;
    /** RC 已接受 L1 请求。 */
    bool powerAccepted = false;
    /** RC 已排入一次拒绝响应，重复 PM 请求不再分配 TLP。 */
    bool powerRejected = false;
    /** EP 已收到 PM_Request_Ack。 */
    bool powerAck = false;
    /** 后续 L1 请求可再次尝试的最早时刻，单位 ps。 */
    uint64_t powerRetry = 0;
    /** 协商途中有新事务等待，L1 进入后立即退出。 */
    bool wakeRequested = false;
    /** 最近事务层活动，单位 ps。 */
    uint64_t lastTransaction = 0;
    /** 请求排空，允许已经在途的完成继续进入。 */
    bool draining = false;
    /** 排空完成并冻结维护。 */
    bool frozen = false;
    /** 排空冻结的时刻，单位 ps。 */
    uint64_t frozenAt = 0;
    /** 当前正在处理 tick，嵌套提交只登记工作。 */
    bool inTick = false;
};

/** 稳定的状态名，用于 trace 和报告。 */
const char *linkStateName(LinkState state);
/** 稳定的观测点名，用于独立检查器。 */
const char *linkPointName(LinkPoint point);

} // namespace gem5::gw100::pcie
#endif
