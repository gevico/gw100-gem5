/**
 * @file  link_transport.hh
 * @brief Host、HDMA、BAR 和中断共享的 PCIe 事务适配器
 *
 * RC 的 tag 分配与 EP 的 HDMA tag 分配分离。EP 直接使用 HDMA 给出的
 * 线上 tag，完成匹配仅使用 Requester ID、tag 和标准完成头字段。
 *
 * @see spec-pcie, spec-host-dma
 */

#ifndef GW100_PCIE_LINK_TRANSPORT_HH
#define GW100_PCIE_LINK_TRANSPORT_HH

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <set>

#include "src/gw100/pcie/hdma/hdma_if.hh"
#include "src/gw100/pcie/link/link_model.hh"

namespace gem5::gw100::pcie
{

/** 目标执行结果，数据只包含请求的实际字节。 */
using TargetDone =
    std::function<void(CompletionStatus, const std::vector<uint8_t> &)>;

/** 原生系统服务，由 endpoint 外壳提供 timing 后端。 */
struct LinkServices
{
    /** 只有目标有限队列能保留完整请求时返回 true。 */
    std::function<bool(unsigned, const Tlp &, TargetDone)> execute;
    /** INTx 消息到达 RC 后改变实际中断线。 */
    std::function<void(bool)> intx;
    /** 目标或链路资源变化，重试 Host PIO、HDMA 和中断。 */
    std::function<void()> ready;
    /** 真实链路状态变化，更新配置空间状态。 */
    std::function<void()> stateChanged;
    /** AER、完成字段与协议错误。 */
    std::function<void(unsigned, LinkError, const std::vector<uint8_t> &)>
        error;
    /** 原始事件观测。 */
    std::function<void(const LinkTrace &)> observe;
    /** 链路与事务后端均已排空。 */
    std::function<void()> drained;
};

/** 已排空事务层与链路层的共同快照。 */
struct TransportSnapshot
{
    /** 双端协议状态。 */
    LinkSnapshot link;
    /** 精确的硬件 Requester ID。 */
    uint16_t endpointId = 0;
    /** 功能主动业务门控。 */
    bool enabled = false;
    /** RC 已经收到的 INTx 电平。 */
    bool intx = false;
    /** 下一 RC tag 分配起点。 */
    unsigned nextTag = 0;
};

/** 有限请求者账本、完成重组与目标完成拆包。 */
class LinkTransport : public HdmaHostIf, public LinkPeerIf
{
  public:
    /** 构造共享双向链路，start() 由所属设备启动时调用。 */
    LinkTransport(LinkClockIf &clock, const LinkConfig &config,
                  uint16_t endpoint_id, Tick timeout, Tick quarantine,
                  unsigned rcb, unsigned completion_bytes,
                  const LinkServices &services,
                  unsigned crs_retries = pciedefs::RC_CRS_RETRIES);
    /** 推进链路、目标结果及隔离期限。 */
    void tick();
    /** Host PIO 的本地准入，posted 写调用者在接受后结算 CPU 响应。 */
    bool submitPio(Tlp &request, HostReadDone done);
    /** 旧调用缺少真实 tag，精确路径要求使用 readTagged。 */
    void read(Addr addr, unsigned len, HostReadDone done) override;
    /** 指定完整 MRd 当前能否被本地发送端接受。 */
    bool canRead(Addr addr, unsigned len) const override;
    /** 接受真实 tag 与 Attr[2:0]，在 DLL 提交时调用 committed。 */
    bool readTagged(Addr addr, unsigned len, uint8_t tag,
                    std::function<void()> committed, HostReadDone done,
                    uint8_t attributes = 0) override;
    /** 发送通路能否再接受一个写请求。 */
    bool canWrite() const override;
    /** 发出一个 MWr，载荷归共享有限发送队列所有。 */
    void write(Addr addr, unsigned len, const uint8_t *data,
               uint8_t attributes = 0) override;
    /** 撤销尚未提交到 DLL 的读，释放本地预留。 */
    void cancelRead(uint8_t tag) override;
    /** 已提交读进入隔离，保留匹配元数据并释放数据预留。 */
    void isolateRead(uint8_t tag) override;
    /** 复位或电源门控撤销尚未提交的主动业务。 */
    void cancelPending() override;
    /** INTx 逻辑电平变化，有限状态寄存器等待共同发送队列。 */
    void setIntx(bool asserted);
    /** 功能门控结束后允许主动请求，配置与完成独立服务。 */
    void setActive(bool enabled);
    /** 有限错误状态锁存器经共同有序队列发送 AER Message。 */
    void sendErrorMessage(uint8_t code);
    /** 更新后续事务的 MPS 和 Extended Synch。 */
    bool configure(unsigned mps, bool extended_synch);
    /** 请求者与目标后端是否已经排空。 */
    bool quiescent() const override;
    /** 查询精确链路，生命周期由所属硬件信号驱动。 */
    LinkModel &
    model()
    {
        return link;
    }
    /** 查询精确链路的只读状态。 */
    const LinkModel &
    model() const
    {
        return link;
    }
    /** 指定侧仍等待 completion 的读，包括隔离元数据。 */
    unsigned outstanding(unsigned side) const;
    /** 已提交且尚未收到完成或超时的请求，对应 Transactions Pending。 */
    unsigned pendingReads(unsigned side) const;
    /** 当前目标之前的 posted 操作均已对后续读取可见。 */
    bool precedingPostedVisible(unsigned side, uint64_t cookie) const;
    /** 保存没有在途事务的完整链路与事务层状态。 */
    TransportSnapshot snapshot() const;
    /** 恢复全部排空状态，源和目标由 drainResume 继续运行。 */
    void restore(const TransportSnapshot &snapshot);

  private:
    /** 真实线上 Requester ID 与 tag 组成的键。 */
    using Key = std::pair<uint16_t, uint8_t>;
    /** 请求者预留资源和匹配状态。 */
    struct Read
    {
        /** 原始线上请求。 */
        Tlp request;
        /** 数据接收缓冲。 */
        std::vector<uint8_t> data;
        /** 已收到的实际请求字节数。 */
        unsigned received = 0;
        /** DWORD 对齐的完成数据预留，单位 B。 */
        unsigned reserved = 0;
        /** 最坏合法拆包的头部预留。 */
        unsigned headers = 0;
        /** 当前已经进入 DLL。 */
        bool submitted = false;
        /** 已超时或功能复位，忽略数据效果。 */
        bool isolated = false;
        /** 完成或隔离截止，单位 ps。 */
        uint64_t deadline = UINT64_MAX;
        /** CRS 后再次提交的期限，单位 ps，无重试时为最大值。 */
        uint64_t retryAt = UINT64_MAX;
        /** 已经执行的 CRS 重试次数。 */
        unsigned retries = 0;
        /** 第一次提交到 DLL 的回调。 */
        std::function<void()> committed;
        /** 完成全部线上校验后调用。 */
        HostReadDone done;
    };
    /** 一个目标后端执行槽。 */
    struct Target
    {
        /** 原始线上请求。 */
        Tlp request;
        /** 所属物理链路代次，仅控制仿真回调寿命。 */
        uint64_t generation = 0;
        /** 目标结果已返回。 */
        bool ready = false;
        /** 目标完成状态。 */
        CompletionStatus status = CompletionStatus::Success;
        /** 待读取者返回的实际字节。 */
        std::vector<uint8_t> data;
        /** 已进入完成发送队列的字节数。 */
        unsigned offset = 0;
    };
    /** 为指定侧保留完整 non-posted 请求并提交。 */
    bool submitRead(unsigned side, Tlp &request,
                    std::function<void()> committed, HostReadDone done);
    /** 接收正常 completion 或可明确报告的错误。 */
    void completion(unsigned side, const Tlp &tlp);
    /** 结算请求者，先释放本地资源再调用上游。 */
    void finish(unsigned side, const Key &key, HostStatus status);
    /** 释放一次完成预留。 */
    void unreserve(unsigned side, Read &read);
    /** 停止上游通知并保留真实 tag，直到迟到完成或隔离期限。 */
    void isolate(unsigned side, Read &read);
    /** 将目标结果逐包提交到共享链路。 */
    void pumpTargets();
    /** 检查 RC 完成超时和 EP/RC 隔离元数据期限。 */
    void checkDeadlines();
    /** 在有限目标执行槽中接收完整 TLP。 */
    bool receive(unsigned side, const Tlp &tlp,
                 std::function<void()> release) override;
    /** 源端 TLP 已进入数据链路层。 */
    void committed(unsigned side, uint64_t cookie) override;
    /** 发送资源变化，可重试本地准入。 */
    void transmitReady(unsigned side) override;
    /** 物理复位作废本地事务，submitted 指该 TLP 曾经进入 DLL。 */
    void cancelled(unsigned side, uint64_t cookie, bool submitted) override;
    /** 错误诊断，header 可为空。 */
    void error(unsigned side, LinkError kind,
               const std::vector<uint8_t> &header) override;
    /** 链路状态、速率或 DLL 就绪状态变化。 */
    void stateChanged() override;
    /** 可选的事件级观测。 */
    void observe(const LinkTrace &trace) override;
    /** 排空完成时通知所属 SimObject。 */
    void drained() override;
    /** 外部 1 ps 时钟。 */
    LinkClockIf &clock;
    /** 原生 timing 目标与观测回调。 */
    LinkServices services;
    /** 双向协议内核。 */
    LinkModel link;
    /** 实际 EP BDF。 */
    const uint16_t endpointId;
    /** 本地完成超时，单位 ps。 */
    const Tick timeout;
    /** 迟到 tag 隔离时间，单位 ps。 */
    const Tick quarantine;
    /** RC 配置请求允许的 CRS 重试次数。 */
    const unsigned crsRetries;
    /** 完成边界，64/128 B。 */
    const unsigned rcb;
    /** 最大完成载荷，64/128/256 B。 */
    const unsigned completionBytes;
    /** 当前有效 MPS，单位 B。 */
    unsigned mps = 128;
    /** 功能逻辑允许主动业务。 */
    bool enabled = false;
    /** 当前期望的 INTx 电平。 */
    bool intxDesired = false;
    /** 最近进入共同发送队列的 INTx 电平。 */
    bool intxSubmitted = false;
    /** 待发 ERR_COR、ERR_NONFATAL、ERR_FATAL 各一个锁存位。 */
    unsigned errorMessages = 0;
    /** 当前物理链路代次。 */
    uint64_t generation = 0;
    /** RC 的下一 tag 搜索起点。 */
    unsigned nextTag = 0;
    /** 双端请求者账本，EP 直接使用 HDMA 分配的 tag。 */
    std::array<std::map<Key, Read>, 2> reads;
    /** 双端当前完成数据预留，单位 B。 */
    std::array<unsigned, 2> reserved{};
    /** 双端当前完成头部预留。 */
    std::array<unsigned, 2> headerReservations{};
    /** 两端各最多六十四个目标槽。 */
    std::array<std::deque<std::shared_ptr<Target>>, 2> targets;
    /** 每个 RX NP 槽记录首次到达时尚未可见的 posted 目标依赖。 */
    std::array<std::map<uint64_t, std::vector<std::weak_ptr<Target>>>, 2>
        postedDependencies;
    /** 尚未进入 DLL、可以因功能门控而撤销的 EP posted 写。 */
    std::set<uint64_t> postedBeforeCommit;
};

} // namespace gem5::gw100::pcie
#endif
