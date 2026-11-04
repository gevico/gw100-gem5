/**
 * @file  int_ctrl.hh
 * @brief 中断控制器：MSI-X 的待发、来源、Pending 与发送，INTx 的电平
 *
 * 按 spec-pcie §8.4 实现：StatusUnit 报告 INT_STATUS 的中断事件和实际
 * 清零的位；MSI-X 使能时，事件置位所对应向量的待发位 W 并记录来源 S，
 * 条件满足时经与 DMA 写共用的发送通路（HdmaHostIf::write）发出一条
 * 消息；被 Function Mask、表项 Mask 或 Bus Master Enable 阻止时 Pending
 * 粘滞置位；软件清零使 S 变空时撤销待发。MSI-X 未使能时按
 * INT_STATUS & ~INT_MASK 和 Interrupt Disable 驱动 INTA。FLR 门控期间
 * 不发消息、不置 INTA。CTX_VECTOR 也在这里。
 *
 * @see spec-pcie
 */

#ifndef __GW100_PCIE_INTR_INT_CTRL_HH__
#define __GW100_PCIE_INTR_INT_CTRL_HH__

#include <array>
#include <cstdint>
#include <functional>

#include "base/types.hh"
#include "src/gw100/pcie/hdma/hdma_if.hh"
#include "src/gw100/pcie/status/status_unit.hh"

namespace gem5
{
namespace gw100
{

/** 中断控制器 */
class IntCtrl
{
  public:
    /** 表项个数的上限 */
    static constexpr unsigned MaxVectors = 32;
    /** 上下文个数的上限 */
    static constexpr unsigned MaxContexts = 16;

    /**
     * 构造：status 提供 INT_STATUS 与中断事件，host 是发送通路，
     * vectors 为表项个数，contexts 为上下文个数
     */
    IntCtrl(StatusUnit &status, HdmaHostIf &host, unsigned vectors,
            unsigned contexts);

    /** 表项个数 */
    unsigned vectors() const { return nvec; }

    /** 读表项 v 的第 w 个 32 位字（0 地址低、1 地址高、2 数据、3 控制） */
    uint32_t tableRead(unsigned v, unsigned w) const;

    /** 写表项 v 的第 w 个 32 位字 */
    void tableWrite(unsigned v, unsigned w, uint32_t val);

    /** PBA 的低 32 位（向量 0 – 31） */
    uint32_t pba() const { return pend; }

    /** MSI-X Enable 与 Function Mask 的当前值（配置写之后调用） */
    void setMsix(bool enable, bool func_mask);

    /** Bus Master Enable 的当前值 */
    void setBusMaster(bool en);

    /** Command 的 Interrupt Disable 的当前值 */
    void setIntxDisable(bool dis);

    /** FLR 门控（spec-pcie §13.2）：期间不发消息、不置 INTA */
    void setFlrGate(bool gate);

    /** 固件写 INT_REQ；FLR 门控期间被忽略 */
    void request(uint32_t bits);

    /** 固件写 INT_LEVEL；FLR 门控期间被忽略 */
    void setLevel(uint32_t v);

    /** INT_LEVEL 的当前值 */
    uint32_t levelReg() const { return status.intLevel(); }

    /** CTX_VECTOR[n] */
    uint32_t ctxVector(unsigned n) const { return ctxVec[n]; }

    /** 固件写 CTX_VECTOR[n]；FLR 门控期间被忽略 */
    void setCtxVector(unsigned n, uint32_t v);

    /** 上下文个数 */
    unsigned contexts() const { return nctx; }

    /**
     * 复位（FLR、设备完全复位）：表项恢复复位值（Mask = 1），待发、
     * 来源、Pending 清零，CTX_VECTOR 恢复复位值；MSI-X 的配置由所属
     * 设备随后经 setMsix() 告知
     */
    void reset();

    /** 发出可以发出的消息（发送通路空闲、条件变化时调用） */
    void trySend();

    /** INTA 的电平变化时调用 */
    std::function<void(bool)> onIntx;

  private:
    /** 一个表项 */
    struct Entry
    {
        /** Message Address 低 32 位 */
        uint32_t addrLo = 0;
        /** Message Address 高 32 位 */
        uint32_t addrHi = 0;
        /** Message Data */
        uint32_t data = 0;
        /** Vector Control：[0] Mask */
        uint32_t ctrl = 1;
    };

    /** INT_STATUS 的位 b 对应的向量（事件时的映射） */
    unsigned vectorOf(unsigned b) const;

    /** StatusUnit 报告的中断事件与清零 */
    void intEvent(uint32_t events, uint32_t cleared);

    /** 按当前状态更新 INTA */
    void updateIntx();

    /** 向量 v 是否被 Function Mask、表项 Mask 或 BME 阻止 */
    bool blocked(unsigned v) const;

    /** 撤销全部待发、来源与 Pending */
    void clearPending();

    /** INT_STATUS 与中断事件 */
    StatusUnit &status;
    /** 发送通路 */
    HdmaHostIf &host;
    /** 表项个数 */
    const unsigned nvec;
    /** 上下文个数 */
    const unsigned nctx;
    /** MSI-X 表 */
    std::array<Entry, MaxVectors> table{};
    /** 待发位 W */
    uint32_t want = 0;
    /** 来源集合 S */
    std::array<uint32_t, MaxVectors> src{};
    /** PBA */
    uint32_t pend = 0;
    /** 轮询位置 */
    unsigned rr = 0;
    /** MSI-X Enable */
    bool msixEn = false;
    /** Function Mask */
    bool funcMask = false;
    /** Bus Master Enable */
    bool bme = false;
    /** Interrupt Disable */
    bool intxDis = false;
    /** FLR 门控 */
    bool flrGate = false;
    /** INTA 的当前电平 */
    bool inta = false;
    /** CTX_VECTOR */
    std::array<uint32_t, MaxContexts> ctxVec{};
};

} // namespace gw100
} // namespace gem5

#endif // __GW100_PCIE_INTR_INT_CTRL_HH__
