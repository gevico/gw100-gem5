/**
 * @file  ml_endpoint.hh
 * @brief ML 一端的适配器：客户端队列、仲裁、tag、屏障、链路状态与错误
 *
 * 两端各有一个 MlEndpoint，经四个 AsyncFifo 相连。本类实现
 * spec-mgmt-link 中与一端有关的全部规则：
 * - 每个客户端一个请求队列，按事务轮询仲裁，没有 tag 的客户端跳过
 *   （§6.2）；屏障事件在此前接受的全部请求发出之后才发出（§5 规则 6）；
 * - 接收方一次只把一个事务交给目标（§5）；
 * - 链路状态 DOWN/UP/FAILED，建立、对端复位、故障（§7）；
 * - 客户端复位时的撤销（§7.4）；超时与通道停滞（§7.5）；
 * - ML_ERR 及第一个错误的记录（§8）。
 * 时间和唤醒经 MlClockIf，不直接依赖 gem5 的事件队列。
 *
 * @see spec-mgmt-link
 */

#ifndef __GW100_ML_ML_ENDPOINT_HH__
#define __GW100_ML_ML_ENDPOINT_HH__

#include <deque>
#include <functional>
#include <string>
#include <vector>

#include "src/gw100/ml/async_fifo.hh"
#include "src/gw100/ml/ml_types.hh"

namespace gem5
{
namespace gw100
{

/** ML 一端的适配器 */
class MlEndpoint
{
  public:
    /** 请求通道 */
    using ReqFifo = AsyncFifo<MlTxnPtr>;
    /** 响应通道 */
    using RspFifo = AsyncFifo<MlRspPtr>;

    /** 一端的配置 */
    struct Config
    {
        /** 本端可用的 tag 数（ml.hif_tags 或 ml.mgmt_tags） */
        unsigned tags = 4;
        /** non-posted 请求的超时与通道停滞的判定时间（ml.timeout） */
        Tick timeout = 0;
        /** 是否为管理 Tile 侧（决定 ML_ERR 是否有 HIF_ERR 镜像位） */
        bool mgmtSide = false;
        /** 对端状态信号的同步器级数 */
        unsigned syncStages = 2;
    };

    /** 构造：name 用于调试输出 */
    MlEndpoint(const std::string &name, const Config &cfg);

    /**
     * 连接本端的时钟、目标、四个通道和对端。tx 是本端发出的方向，
     * rx 是本端接收的方向
     */
    void bind(MlClockIf *clk, MlTargetIf *tgt, ReqFifo *tx_req,
              RspFifo *tx_rsp, ReqFifo *rx_req, RspFifo *rx_rsp,
              MlEndpoint *peer_ep);

    /** 登记一个客户端，返回它的编号 */
    unsigned addClient();

    /**
     * 客户端提交一个请求。返回 false 表示链路尚未建立（DOWN），适配器
     * 不接受，客户端须自行保留；故障态下立即结束（non-posted 以 FAULT
     * 结束，posted 写丢弃）并返回 true
     */
    bool submit(unsigned client, const MlTxnPtr &txn);

    /** 客户端复位：撤销该客户端已被接受的全部请求（§7.4） */
    void withdraw(unsigned client);

    /** 本端复位置位：清除全部状态，就绪信号变为 0 */
    void resetAssert();

    /** 本端复位撤销：送出就绪信号 */
    void resetRelease();

    /** 只在 HIF 侧：设置送往对端的"ML_HIF_ERR 非零"电平 */
    void setHifErrLevel(bool level);

    /** 本端在当前时钟沿完成能做的全部工作，并请求下一次唤醒 */
    void process();

    /** 链路状态 */
    MlLinkState state() const { return linkState; }

    /** ML_STATUS：[0] UP，[1] FAILED */
    uint32_t statusReg() const;

    /** ML_ERR：[7:0] 粘滞位；管理 Tile 侧另有 [8] HIF_ERR 镜像 */
    uint32_t errReg() const;

    /** 对 ML_ERR 写 1 清零 [7:0]；[8] 不可写 */
    void clearErr(uint32_t mask);

    /** ML_ERR_INFO：[3:0] 事务编码，[8:4] tag，[13:9] 位号 */
    uint32_t errInfoReg() const { return errInfo; }

    /** ML_ERR_ADDR：第一个错误的事务地址（低 32 位） */
    uint32_t errAddrReg() const { return errAddr; }

    /** ML_ERR 或链路状态变化时调用，用于更新中断或 DEV_FATAL */
    std::function<void()> onChange;

    /** 当前在途（已发出、未结束）的 non-posted 请求数，供测试 */
    unsigned outstanding() const;

    /** 调试输出用的名字 */
    const std::string name;

  private:
    /** 一个已分配的 tag */
    struct TagEntry
    {
        /** 是否在用 */
        bool inUse = false;
        /** 对应的事务 */
        MlTxnPtr txn;
        /** 第一拍写入请求通道的时刻，用于超时；尚未写入时为 MaxTick */
        Tick issued = MaxTick;
    };

    /** 对端状态信号的一次变化，经同步器后生效 */
    struct SigChange
    {
        /** 生效时刻 */
        Tick at;
        /** 0 就绪，1 故障，2 HIF_ERR */
        unsigned sig;
        /** 新电平 */
        bool level;
    };

    /** 对端调用：它的某个状态信号在时刻 t 变为 level */
    void peerSignal(unsigned sig, bool level, Tick t);

    /** 向对端送出状态信号 */
    void sendSignal(unsigned sig, bool level);

    /** 检查客户端请求是否满足 §4.3 的长度与对齐规则 */
    static bool validRequest(const MlTxn &t);

    /** 以 status 结束一个 non-posted 请求（已撤销的不调用回调） */
    static void finish(const MlTxnPtr &t, MlStatus status,
                       const MlData &data);

    /** 记录一个错误位；ML_ERR [7:0] 为 0 时同时记录事务信息 */
    void recordErr(unsigned bit, const MlTxn *t);

    /** 进入故障态（§7.5）；bit 为本地原因 */
    void fail(unsigned bit, const MlTxn *t);

    /** 结束本端全部已接受的请求：non-posted 以 status 结束 */
    void abortAll(MlStatus status);

    /** 应用已生效的对端信号，必要时改变链路状态 */
    void applySignals(Tick now);

    /** 接收响应通道中的一拍 */
    void receiveResponse(Tick now);

    /** 接收请求通道中的一拍；收齐后交给目标 */
    void receiveRequest(Tick now);

    /** 向响应通道写一拍 */
    void sendResponse(Tick now);

    /** 向请求通道写一拍；必要时先仲裁出下一个事务 */
    void sendRequest(Tick now);

    /**
     * 轮询各客户端，选出下一个可以发出的请求；没有时返回空。pop 为
     * false 时只判断，不取出
     */
    MlTxnPtr arbitrate(bool pop);

    /** 目标处理完 txn 后的处理；gen 不等于当前代时忽略（已复位） */
    void onTargetDone(const MlTxnPtr &txn, MlStatus status,
                      const MlData &data, uint64_t gen);

    /** 检查 tag 超时 */
    void checkTimeouts(Tick now);

    /** 计算并请求下一次唤醒 */
    void scheduleNext(Tick now);

    /** 本端配置 */
    const Config cfg;
    /** 本端时钟 */
    MlClockIf *clk = nullptr;
    /** 本端目标 */
    MlTargetIf *target = nullptr;
    /** 本端发出的请求通道 */
    ReqFifo *txReq = nullptr;
    /** 本端发出的响应通道 */
    RspFifo *txRsp = nullptr;
    /** 本端接收的请求通道 */
    ReqFifo *rxReq = nullptr;
    /** 本端接收的响应通道 */
    RspFifo *rxRsp = nullptr;
    /** 对端 */
    MlEndpoint *peer = nullptr;

    /** 链路状态 */
    MlLinkState linkState = MlLinkState::Down;
    /** 本端是否处于复位中 */
    bool inReset = true;
    /** 同步后看到的对端就绪信号 */
    bool peerReady = false;
    /** 同步后看到的对端故障信号 */
    bool peerFailed = false;
    /** 同步后看到的 HIF_ERR 电平（只在管理 Tile 侧有意义） */
    bool hifErrMirror = false;
    /** 尚未生效的对端信号变化，按时刻排序 */
    std::deque<SigChange> sigQueue;

    /** 各客户端的请求队列 */
    std::vector<std::deque<MlTxnPtr>> queues;
    /** 轮询仲裁的下一个起点 */
    unsigned rrNext = 0;
    /** 下一个被接受请求的顺序号 */
    uint64_t nextSeq = 1;
    /** tag 表 */
    std::vector<TagEntry> tagTable;

    /** 正在写入请求通道的事务 */
    MlTxnPtr curTx;
    /** 下一拍的拍号 */
    unsigned curTxBeat = 0;
    /** 有拍等待写入、通道没有空位的起始时刻；MaxTick 表示没有停滞 */
    Tick stallSince = MaxTick;

    /** 待发出的响应 */
    std::deque<MlRspPtr> rspOut;
    /** 正在写入的响应的下一拍拍号 */
    unsigned curRspBeat = 0;

    /** 正在交给目标处理的请求；处理完毕前不读下一个 */
    MlTxnPtr curRx;
    /** curRx 是否已收齐并交给了目标 */
    bool curRxDelivered = false;

    /** ML_ERR 的 [7:0] */
    uint32_t err = 0;
    /** ML_ERR_INFO */
    uint32_t errInfo = 0;
    /** ML_ERR_ADDR */
    uint32_t errAddr = 0;
    /** 最近一次向请求通道写入的时刻：每个周期最多一拍 */
    Tick lastTxReq = MaxTick;
    /** 最近一次向响应通道写入的时刻 */
    Tick lastTxRsp = MaxTick;
    /** 最近一次从请求通道读出的时刻 */
    Tick lastRxReq = MaxTick;
    /** 最近一次从响应通道读出的时刻 */
    Tick lastRxRsp = MaxTick;
    /** 代号：复位、故障、对端复位时加 1，使迟到的目标回调失效 */
    uint64_t gen = 0;
};

} // namespace gw100
} // namespace gem5

#endif // __GW100_ML_ML_ENDPOINT_HH__
