/**
 * @file  memory_tile.hh
 * @brief 双通道 Memory Tile 的 timing 外壳
 *
 * @see spec-dram, spec-clock-reset-power
 */

#ifndef GW100_MEMORY_TILE_HH
#define GW100_MEMORY_TILE_HH

#include <array>
#include <deque>
#include <memory>
#include <unordered_set>
#include <vector>

#include "mem/mem_ctrl.hh"
#include "mem/port.hh"
#include "params/Gw100MemoryTile.hh"
#include "sim/serialize.hh"
#include "src/gw100/mem_tile/memory_control.hh"
#include "src/gw100/mem_tile/memory_link.hh"

namespace gem5::gw100
{

class Gw100MemoryIngress;

/** 双通道 Memory Tile */
class Gw100MemoryTile : public ClockedObject
{
  public:
    PARAMS(Gw100MemoryTile);
    /** 绑定两个原生控制器和 DDR 时钟 */
    explicit Gw100MemoryTile(const Params &p);
    /** 查找控制或数据端口 */
    Port &getPort(const std::string &name,
                  PortID idx = InvalidPortID) override;
    /** 发布数据和控制窗口 */
    void init() override;
    /** 恢复 checkpoint 的媒体状态 */
    void startup() override;
    /** 排空全部入口和本地事务，然后保持媒体 */
    DrainState drain() override;
    /** 恢复被 checkpoint 排空的媒体 */
    void drainResume() override;
    /** 保存已排空的寄存器和状态 */
    void serialize(CheckpointOut &cp) const override;
    /** 恢复控制状态，媒体由 startup 重建 */
    void unserialize(CheckpointIn &cp) override;
    /** CRC 更新 NoC、Memory Tile 复位和 PLL 状态 */
    void setControl(bool released, bool locked);
    /** 断言内存复位，收尾旧代次 */
    void resetMemory();
    /** 注册硬件入口，用于整个子系统的排空 */
    void attachIngress(Gw100MemoryIngress *ingress);
    /** 请求外壳继续处理 */
    void wake(Tick when = 0);
    /** 检查请求，已准入事务可在排空期间完成后续子请求 */
    unsigned checkAccess(Addr addr, unsigned size, unsigned source,
                         bool admitted = false) const;
    /** 调试读取控制快照 */
    uint64_t readRegister(uint64_t offset) const;
    /** 当前内存复位代次 */
    uint64_t
    epoch() const
    { return control.epoch; }
    /** 该 Tile 的控制寄存器基址 */
    Addr registerBase() const;
    /** 记录入口预检拒绝的完整事务 */
    void recordError(unsigned error, unsigned source, Addr address,
                     uint64_t transaction);

  private:
    /** 核心互连响应端口 */
    class FrontPort : public ResponsePort
    {
      public:
        /** 绑定所属 Tile */
        FrontPort(const std::string &name, Gw100MemoryTile &tile);
        /** 返回窗口 */
        AddrRangeList getAddrRanges() const override;
        /** 接受 timing 请求或反压 */
        bool recvTimingReq(PacketPtr pkt) override;
        /** 调试只读控制寄存器 */
        void recvFunctional(PacketPtr pkt) override;
        /** 精确配置拒绝 atomic 数据路径 */
        Tick recvAtomic(PacketPtr pkt) override;
        /** 返回路径恢复 */
        void recvRespRetry() override;

      private:
        /** 所属 Tile */
        Gw100MemoryTile &tile;
    };
    /** 一次已接受的原生 burst 操作 */
    struct DataTxn
    {
        /** 原始子请求和 CDC 槽位 */
        MemoryLink::ItemPtr item;
        /** 所属通道 */
        unsigned channel = 0;
        /** 对齐的原生 burst 地址 */
        Addr burst = 0;
        /** 接受时的内存代次 */
        uint64_t epoch = 0;
        /** 需要读改写 */
        bool rmw = false;
        /** 已进入媒体队列 */
        bool issued = false;
    };
    /** 在媒体响应中保持事务 */
    struct MediaState : public Packet::SenderState
    {
        /** 对应的前端事务 */
        std::shared_ptr<DataTxn> txn;
        /** 本次为 RMW 的读阶段 */
        bool rmwRead = false;
    };
    /** 一个通道到原生控制器的端口 */
    class MediaPort : public RequestPort
    {
      public:
        /** 绑定所属 Tile */
        MediaPort(const std::string &name, Gw100MemoryTile &tile);
        /** 发出媒体包，满时持有重试 */
        void send(PacketPtr pkt);
        /** 所有已发送的请求已被接收 */
        bool
        idle() const
        { return waiting.empty(); }

      protected:
        /** 返回媒体结果 */
        bool recvTimingResp(PacketPtr pkt) override;
        /** 原生控制器撤销反压 */
        void recvReqRetry() override;

      private:
        /** 所属 Tile */
        Gw100MemoryTile &tile;
        /** 待重试包，受前端槽位约束 */
        std::deque<PacketPtr> waiting;
    };
    /** 有界的寄存器或错误响应 */
    struct Reply
    {
        /** 响应包 */
        PacketPtr packet;
        /** 最早发送时刻 */
        Tick ready;
    };
    /** 处理前端请求 */
    bool receive(PacketPtr pkt);
    /** 在请求实际到达后执行控制寄存器访问 */
    void accessControl(PacketPtr pkt);
    /** 返回一个明确错误 */
    void reject(PacketPtr pkt, unsigned error);
    /** 处理事件、RMW 与生命周期 */
    void process();
    /** 通过共享前端端口发送响应，保持统一的背压状态 */
    bool sendResponse(PacketPtr pkt);
    /** 在 DDR 接收时钟端推进一个通道的 burst 队列 */
    void processChannel(unsigned ch);
    /** 执行一个已接受的异步命令 */
    void beginCommand(unsigned ch, unsigned cmd);
    /** 取得校准或训练反馈 */
    void feedback(unsigned ch, unsigned cmd, uint64_t epoch);
    /** 发出一项原生读或写 */
    void issue(const std::shared_ptr<DataTxn> &txn, bool rmw_read,
               const uint8_t *data = nullptr);
    /** 完成原生媒体访问 */
    void mediaResponse(PacketPtr pkt);
    /** 返回一个数据操作并释放 burst 锁 */
    void finish(const std::shared_ptr<DataTxn> &txn, bool ok);
    /** 检查旧代次是否仍有效 */
    bool valid(const std::shared_ptr<DataTxn> &txn) const;
    /** 处理一个通道的排空或恢复 */
    void maintenance(unsigned ch);
    /** 检查可否结束全局排空，notify 控制异步完成通知 */
    void checkDrain(bool notify = true);

    /** 软件控制与保护 */
    MemoryControl control;
    /** 核心侧端口 */
    FrontPort front;
    /** 两个原生控制器 */
    std::array<memory::MemCtrl *, 2> controllers;
    /** 两个媒体端口 */
    std::array<std::unique_ptr<MediaPort>, 2> media;
    /** 两个核心到 DDR 的双向 FIFO */
    std::array<std::unique_ptr<MemoryLink>, 2> links;
    /** 每通道等待 burst 锁的事务 */
    std::array<std::deque<std::shared_ptr<DataTxn>>, 2> waiting;
    /** 正在执行的 burst */
    std::array<std::unordered_set<Addr>, 2> lockedBursts;
    /** 生命周期开销尚待执行的命令 */
    std::array<unsigned, 2> pendingCommand{};
    /** 本地复位尚待媒体收尾 */
    std::array<bool, 2> resetting{};
    /** checkpoint 已取得媒体保持 */
    std::array<bool, 2> drainHeld{};
    /** 已恢复但媒体尚待重建 */
    std::array<bool, 2> restoring{};
    /** 寄存器和拒绝响应队列，最多 64 项 */
    std::deque<Reply> replies;
    /** 尚待互连到达和译码的控制请求，最多 64 项 */
    std::deque<Reply> controls;
    /** 核心响应被反压 */
    bool replyBlocked = false;
    /** 核心请求需要 retry */
    bool requestRetry = false;
    /** 全局排空进行中 */
    bool draining = false;
    /** 从 checkpoint 载入 */
    bool restored = false;
    /** CRC 请求释放，但可能仍在收尾旧媒体 */
    bool releaseRequested = false;
    /** 本轮全局排空已经完成 */
    bool drainComplete = false;
    /** 上游来源入口 */
    std::vector<Gw100MemoryIngress *> ingresses;
    /** 本地处理事件 */
    EventFunctionWrapper event;
    /** 每通道训练事件 */
    std::array<std::unique_ptr<EventFunctionWrapper>, 2> training;
    /** 反馈校准时延 */
    const Tick calibrationLatency;
    /** 反馈训练时延 */
    const Tick trainingLatency;
    /** 逐 rank 反馈失败掩码 */
    const uint64_t failMask;
    /** 逐 rank 无反馈掩码 */
    const uint64_t timeoutMask;
    /** 注入反馈故障的命令 */
    const unsigned faultPhase;
    /** 任意 rank 的首次自检读取注入错误，兼容原有开关 */
    bool corruptSelftest;
    /** Tile 内每个 rank 注入一次自检错误的四位掩码 */
    uint64_t selftestMask;
};

} // namespace gem5::gw100
#endif
