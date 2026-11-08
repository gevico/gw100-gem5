/**
 * @file  hdma_if.hh
 * @brief HdmaEngine 与外界的接口：时钟、host 内存、设备内存、ML
 *
 * HdmaEngine 只依赖这几个接口，不接触 gem5 的 Packet 和端口，便于单元
 * 测试。所属设备实现它们：host 内存经 PCIe 的 DMA 端口，设备内存经
 * mem_port，描述符和事件经 ML 的 HIF 侧适配器。
 *
 * @see spec-host-dma
 */

#ifndef __GW100_PCIE_HDMA_HDMA_IF_HH__
#define __GW100_PCIE_HDMA_HDMA_IF_HH__

#include <cstdint>
#include <functional>
#include <utility>

#include "base/types.hh"
#include "src/gw100/ml/ml_types.hh"

namespace gem5
{
namespace gw100
{

/** HDMA 的时钟（clk_pcie） */
class HdmaClockIf
{
  public:
    virtual ~HdmaClockIf() = default;

    /** 当前时刻 */
    virtual Tick now() const = 0;

    /** 时钟周期 */
    virtual Tick period() const = 0;

    /** 请求在不早于 when 的第一个时钟沿调用 HdmaEngine::tick() */
    virtual void wakeAt(Tick when) = 0;
};

/** host 读请求的完成状态（spec-host-dma §8.1） */
enum class HostStatus : uint8_t
{
    /** 成功 */
    Ok,
    /** Unsupported Request */
    Ur,
    /** Completer Abort */
    Ca,
    /** poisoned 完成 */
    Poisoned,
};

/** host 读请求的完成回调：状态与读到的数据（失败时无意义） */
using HostReadDone = std::function<void(HostStatus, const uint8_t *)>;

/** HDMA 经 PCIe 访问 host 内存（HDMA 是请求方） */
class HdmaHostIf
{
  public:
    virtual ~HdmaHostIf() = default;

    /**
     * 发出一个 MRd：len 不超过读请求的上限，不跨 4 KiB；全部完成数据
     * 到达时调用 done。完成超时由 HdmaEngine 自己计时
     */
    virtual void read(Addr addr, unsigned len, HostReadDone done) = 0;

    /** 指定完整 MRd 当前能否被本地发送端接受。 */
    virtual bool
    canRead(Addr, unsigned) const
    {
        return true;
    }

    /** 接受真实 tag 与 Attr[2:0]，在 DLL 提交时调用 committed。 */
    virtual bool
    readTagged(Addr addr, unsigned len, uint8_t,
               std::function<void()> committed, HostReadDone done, uint8_t = 0)
    {
        committed();
        read(addr, len, std::move(done));
        return true;
    }

    /** 撤销尚未提交到 DLL 的读，释放本地预留。 */
    virtual void
    cancelRead(uint8_t)
    {}

    /** 已提交读进入隔离，保留匹配元数据并释放数据预留。 */
    virtual void
    isolateRead(uint8_t)
    {}

    /** 复位或电源门控撤销尚未提交的主动业务。 */
    virtual void
    cancelPending()
    {}

    /** 发送通路能否再接受一个写请求 */
    virtual bool canWrite() const = 0;

    /** 发出一个 MWr：len 不超过 MPS，不跨 4 KiB；交给发送通路即返回 */
    virtual void write(Addr addr, unsigned len, const uint8_t *data,
                       uint8_t attributes = 0) = 0;
};

/** 设备内存读的完成回调：是否成功与读到的数据 */
using DevReadDone = std::function<void(bool, const uint8_t *)>;

/** 设备内存写的完成回调：是否成功（收到写确认） */
using DevWriteDone = std::function<void(bool)>;

/** HDMA 访问设备一侧的存储（DPA 空间） */
class HdmaDevIf
{
  public:
    virtual ~HdmaDevIf() = default;

    /** 能否再接受一个请求；不能时由所属设备在可以时唤醒 HDMA */
    virtual bool canSend() const = 0;

    /** 读 DPA 起的 len 字节 */
    virtual void read(Addr dpa, unsigned len, DevReadDone done) = 0;

    /** 写 DPA 起的 len 字节，收到写确认时调用 done */
    virtual void write(Addr dpa, unsigned len, const uint8_t *data,
                       DevWriteDone done) = 0;
};

/** DESC_RD 的完成回调：状态与 32 B 的描述符 */
using DescReadDone = std::function<void(MlStatus, const MlData &)>;

/** DESC_WR 的完成回调：状态 */
using DescWriteDone = std::function<void(MlStatus)>;

/** HDMA 经 ML 访问管理 Tile（HDMA 是 ML 的客户端） */
class HdmaMlIf
{
  public:
    virtual ~HdmaMlIf() = default;

    /** 读 32 B 的描述符；返回 false 表示链路尚未建立，稍后重试 */
    virtual bool descRead(Addr addr, DescReadDone done) = 0;

    /** non-posted 回写 4 B 的 DW7；返回 false 表示链路尚未建立 */
    virtual bool descWrite(Addr addr, uint32_t dw7, DescWriteDone done) = 0;

    /** posted 的 EVENT，置位 bits；返回 false 表示链路尚未建立 */
    virtual bool event(uint32_t bits) = 0;

    /** 撤销 HDMA 已被 ML 接受的全部请求（复位时，spec-mgmt-link §7.4） */
    virtual void withdraw() = 0;
};

} // namespace gw100
} // namespace gem5

#endif // __GW100_PCIE_HDMA_HDMA_IF_HH__
