/**
 * @file  ml_types.hh
 * @brief 管理链路 ML 的公共类型：事务、响应状态、链路状态、时钟接口
 *
 * ML 的两端（HIF 侧、管理 Tile 侧）和四个通道共用这些类型。核心逻辑
 * 不直接依赖 gem5 的事件队列，而是经 MlClockIf 取时间、请求唤醒，
 * 便于单元测试；SimObject 外壳把它接到 ClockedObject 上。
 *
 * @see spec-mgmt-link
 */

#ifndef __GW100_ML_ML_TYPES_HH__
#define __GW100_ML_ML_TYPES_HH__

#include <array>
#include <cstdint>
#include <functional>
#include <memory>

#include "base/types.hh"

namespace gem5
{
namespace gw100
{

/** 事务类型及其编码（spec-mgmt-link §4.1） */
enum class MlType : uint8_t
{
    /** 启动写入，non-posted 写 */
    BootWr = 0x0,
    /** doorbell 写入，posted 写 */
    DbWr = 0x1,
    /** 事件，posted 写 */
    Event = 0x2,
    /** HDMA 读描述符 */
    DescRd = 0x3,
    /** HDMA 回写描述符，non-posted 写 */
    DescWr = 0x4,
    /** 屏障事件，non-posted */
    Barrier = 0x5,
    /** 固件读 HIF 寄存器 */
    RegRd = 0x8,
    /** 固件写 HIF 寄存器，posted 写 */
    RegWr = 0x9,
};

/** 请求的结果（spec-mgmt-link §4.4） */
enum class MlStatus : uint8_t
{
    /** 已执行完毕 */
    Ok,
    /** 没有任何效果 */
    Reject,
    /** 效果未知，由请求方本地产生 */
    Fault,
};

/** 链路状态（spec-mgmt-link §7.1） */
enum class MlLinkState : uint8_t
{
    /** 复位中或尚未建立 */
    Down,
    /** 已建立 */
    Up,
    /** 故障态，只有复位能退出 */
    Failed,
};

/** ML_ERR 的位号（spec-mgmt-link §8.1） */
enum MlErrBit : unsigned
{
    /** 本端的 non-posted 请求超时 */
    ErrTimeout = 0,
    /** 本端的请求通道停滞 */
    ErrStall = 1,
    /** 收到格式错误的事务或无对应请求的响应 */
    ErrProtocol = 2,
    /** 在途请求因对端复位以 FAULT 结束 */
    ErrPeerReset = 3,
    /** 本端发出的 non-posted 请求收到 REJECT */
    ErrNpReject = 4,
    /** 对端发来的 posted 写被本端的目标拒绝 */
    ErrWrReject = 5,
    /** 本端的客户端提交了不合规则的请求 */
    ErrBadReq = 6,
    /** 因看到对端的故障信号而进入 FAILED */
    ErrPeerFailed = 7,
    /** 只在管理 Tile 侧：HIF 侧的 ML_HIF_ERR 非零（镜像） */
    ErrHifErr = 8,
};

/** 每拍的数据字节数（ml.data_bits = 128） */
constexpr unsigned mlBeatBytes = 16;

/** 一个事务最多的数据字节数（ml.max_payload） */
constexpr unsigned mlMaxPayload = 64;

/** 一次事务的数据：最多 mlMaxPayload 字节 */
using MlData = std::array<uint8_t, mlMaxPayload>;

/** 请求方收到结果时的回调：状态，以及读回的数据（读以外无意义） */
using MlDone = std::function<void(MlStatus, const MlData &)>;

/** 判断事务类型是否为 non-posted：读、BOOT_WR、DESC_WR、BARRIER */
inline bool
mlNonPosted(MlType t)
{
    return t == MlType::BootWr || t == MlType::DescRd ||
           t == MlType::DescWr || t == MlType::Barrier ||
           t == MlType::RegRd;
}

/** 判断事务类型是否为读：DESC_RD、REG_RD */
inline bool
mlIsRead(MlType t)
{
    return t == MlType::DescRd || t == MlType::RegRd;
}

/** 一个 ML 事务：请求方填写，沿请求通道传给对端 */
struct MlTxn
{
    /** 事务类型 */
    MlType type = MlType::Event;
    /** 地址字段，含义随类型而定（spec-mgmt-link §4.1） */
    Addr addr = 0;
    /** 字节数，1 – mlMaxPayload */
    unsigned len = 0;
    /** 写数据（读请求不用） */
    MlData data{};
    /** 字节使能，第 i 位对应 data[i]；只对写有效 */
    uint64_t be = 0;
    /** non-posted 请求的 tag，发出时分配 */
    unsigned tag = 0;
    /** 提交它的客户端编号 */
    unsigned client = 0;
    /** 被适配器接受的顺序号，用于屏障（§5 规则 6） */
    uint64_t seq = 0;
    /** 是否已被撤销（§7.4） */
    bool withdrawn = false;
    /** 请求方收到结果时的回调；posted 写不调用 */
    MlDone done;

    /** 请求在通道上占几拍：带数据的写按长度，其余 1 拍 */
    unsigned
    reqBeats() const
    {
        if (mlIsRead(type) || type == MlType::Barrier)
            return 1;
        return (len + mlBeatBytes - 1) / mlBeatBytes;
    }
};

/** 指向事务的共享指针：请求方与对端在模型中共用同一对象 */
using MlTxnPtr = std::shared_ptr<MlTxn>;

/** 一个 ML 响应：沿响应通道传回请求方 */
struct MlRsp
{
    /** 对应请求的 tag */
    unsigned tag = 0;
    /** OK 或 REJECT */
    MlStatus status = MlStatus::Ok;
    /** 读数据 */
    MlData data{};
    /** 读数据的字节数；写的响应和 REJECT 为 0 */
    unsigned len = 0;

    /** 响应在通道上占几拍：带数据的读 OK 按长度，其余 1 拍 */
    unsigned
    beats() const
    {
        if (status != MlStatus::Ok || len == 0)
            return 1;
        return (len + mlBeatBytes - 1) / mlBeatBytes;
    }
};

/** 指向响应的共享指针 */
using MlRspPtr = std::shared_ptr<MlRsp>;

/**
 * 一端的时钟与唤醒接口：ML 核心逻辑经它取时间、按本端时钟对齐、
 * 请求在将来某个时刻被调用
 */
class MlClockIf
{
  public:
    /** 虚析构 */
    virtual ~MlClockIf() = default;

    /** 当前时刻 */
    virtual Tick now() const = 0;

    /** 时刻 t 之后（不含 t）本端的第一个时钟沿 */
    virtual Tick edgeAfter(Tick t) const = 0;

    /** 本端当前的时钟周期 */
    virtual Tick period() const = 0;

    /** 请求在 when 时刻（不早于下一个时钟沿）调用一次本端的处理函数 */
    virtual void wakeAt(Tick when) = 0;
};

/** 一端的目标收到请求后，用它返回结果；posted 写也要调用，表示处理完毕 */
using MlRespond = std::function<void(MlStatus, const MlData &)>;

/** 一端的目标：接收对端发来的请求 */
class MlTargetIf
{
  public:
    /** 虚析构 */
    virtual ~MlTargetIf() = default;

    /**
     * 处理对端发来的一个请求。处理完毕（写已生效或被拒绝、读已取得
     * 数据）后调用 respond，可以同步调用，也可以稍后调用
     */
    virtual void mlRequest(const MlTxnPtr &txn, MlRespond respond) = 0;
};

} // namespace gw100
} // namespace gem5

#endif // __GW100_ML_ML_TYPES_HH__
