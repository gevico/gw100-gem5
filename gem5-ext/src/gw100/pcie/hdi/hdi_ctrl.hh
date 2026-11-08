/**
 * @file  hdi_ctrl.hh
 * @brief HIF 中 HDI 控制器的部分：影子寄存器、HDI_WR_MAP、BUSY、doorbell
 *
 * host 的读从不经过 ML，读到的是影子寄存器；host 写可写字段时更新影子、
 * 置位 HDI_WR_MAP 并发出 HDI_WR 事件（spec-pcie §6.5）。HDI_CSTS.BUSY
 * 是 HIF 的待处理位与固件写入的 BUSY 位之或（spec-hdi §5.5）。doorbell
 * 写入进入覆盖式的待发槽，按各槽最后一次写入的先后以 DB_WR 发往管理
 * Tile（spec-pcie §5.3）。FLR 门控期间 host 的 HDI 写入和 doorbell
 * 被丢弃，固件的影子写被忽略（spec-pcie §13.2）。
 *
 * @see spec-hdi, spec-pcie
 */

#ifndef __GW100_PCIE_HDI_HDI_CTRL_HH__
#define __GW100_PCIE_HDI_HDI_CTRL_HH__

#include <array>
#include <cstdint>
#include <deque>
#include <functional>

#include "base/types.hh"
#include "sim/serialize.hh"

namespace gem5
{
namespace gw100
{

/** HDI 控制器在 HIF 中的状态 */
class HdiCtrl : public Serializable
{
  public:
    /** doorbell 页数：管理队列 1 页加 16 个上下文 */
    static constexpr unsigned Pages = 17;
    /** doorbell 槽数：每页 SQ_TAIL、CQ_HEAD 两个 */
    static constexpr unsigned Slots = 2 * Pages;
    /** 寄存器偏移：HDI_CC */
    static constexpr Addr Cc = 0x010;
    /** 寄存器偏移：HDI_CSTS */
    static constexpr Addr Csts = 0x014;
    /** 寄存器偏移：HDI_FATAL */
    static constexpr Addr Fatal = 0x030;
    /** 寄存器偏移：HDI_CTX_ERR */
    static constexpr Addr CtxErr = 0x034;
    /** 固件视角：HDI_WR_MAP */
    static constexpr Addr WrMap = 0x100;
    /** HDI_CSTS.BUSY */
    static constexpr uint32_t CstsBusy = 1u << 4;

    /** 构造：寄存器为复位值 */
    HdiCtrl();

    /** host 视角：偏移 off 是否是已定义的寄存器（spec-hdi §4） */
    static bool defined(Addr off);

    /** host 读偏移 off 的寄存器 */
    uint32_t hostRead(Addr off) const;

    /** host 写偏移 off 的寄存器：只更新可写字段 */
    void hostWrite(Addr off, uint32_t v);

    /** host 写 doorbell 页 page 内偏移 off（0 为 SQ_TAIL，4 为 CQ_HEAD） */
    void hostDoorbell(unsigned page, Addr off, uint32_t v);

    /**
     * 固件经 HIF 区读偏移 off（0x000 – 0x0FF 与 HDI_WR_MAP）；其余返回
     * false
     */
    bool fwRead(Addr off, uint32_t &v) const;

    /** 固件经 HIF 区写偏移 off；其余返回 false */
    bool fwWrite(Addr off, uint32_t v);

    /** FLR 门控（spec-pcie §13.2） */
    void setGate(bool g) { gated = g; }

    /** 复位：寄存器、HDI_WR_MAP、BUSY、doorbell 待发槽恢复复位值 */
    void reset();
    /** 保存 HDI 影子寄存器、门控和有界 doorbell 槽。 */
    void serialize(CheckpointOut &cp) const override;
    /** 恢复 HDI 影子寄存器、门控和有界 doorbell 槽。 */
    void unserialize(CheckpointIn &cp) override;

    /** 按顺序发出待发的 doorbell，直到 sendDb 不再接受 */
    void trySendDoorbells();

    /** 发出一个 DB_WR：槽号与值；返回 false 表示链路尚未建立 */
    std::function<bool(unsigned slot, uint16_t value)> sendDb;

    /** host 写了可写字段：发出 HDI_WR 事件 */
    std::function<void()> onHdiWr;

    /**
     * HDI_FATAL ≠ 0 或 HDI_CTX_ERR ≠ 0 的电平变化：INT_STATUS.ERROR 的
     * 硬件源之一（spec-pcie §8.4）
     */
    std::function<void(bool)> onErrLevel;

  private:
    /** 记录一次可写寄存器的 host 写入 */
    void recordWrite(Addr off);

    /** 把 HDI_FATAL、HDI_CTX_ERR 的当前电平交给 onErrLevel */
    void updateErrLevel();

    /** 影子寄存器：0x000 – 0x0FF，按 32 位字 */
    std::array<uint32_t, 64> reg{};
    /** HDI_WR_MAP */
    uint32_t wrMap = 0;
    /** BUSY 的 HIF 待处理位 */
    bool busyPend = false;
    /** BUSY 的固件位 */
    bool busyFw = false;
    /** FLR 门控 */
    bool gated = false;
    /** 待发槽的值 */
    std::array<uint16_t, Slots> slotVal{};
    /** 待发槽的有效位 */
    std::array<bool, Slots> slotValid{};
    /** 有效的槽，按最后一次写入的先后 */
    std::deque<unsigned> order;
};

} // namespace gw100
} // namespace gem5

#endif // __GW100_PCIE_HDI_HDI_CTRL_HH__
