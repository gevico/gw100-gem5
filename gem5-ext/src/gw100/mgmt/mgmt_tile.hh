/**
 * @file  mgmt_tile.hh
 * @brief 管理 Tile 的外壳：本地总线端口、ML 管理侧目标、中断
 *
 * 本类负责与 gem5 的对接：pio 端口的时序、功能访问（加载 ROM 镜像），
 * 以及 PLIC 中断源的电平。各项行为由组件实现：
 * - ROM 与 SRAM：MgmtSram；
 * - 看门狗的状态机：MgmtWdt；本类按 mtime 安排到期事件，第二级超时时
 *   停住管理 CPU 并置位 wdt_fatal 信号；
 * - ML、SRAM 控制、访问控制寄存器：MgmtLocalRegs；
 * - doorbell 信箱与事件寄存器：MgmtMailbox；
 * - 时钟复位控制器的寄存器：MgmtCrc；
 * - 管理 CPU 对 HIF 寄存器区的访问：经 ML 的 REG_RD、REG_WR。
 *
 * @see spec-mgmt-tile, spec-mgmt-link
 */

#ifndef __GW100_MGMT_MGMT_TILE_HH__
#define __GW100_MGMT_MGMT_TILE_HH__

#include <array>
#include <map>
#include <vector>

#include "cpu/base.hh"
#include "dev/intpin.hh"
#include "dev/riscv/plic.hh"
#include "mem/qport.hh"
#include "params/Gw100MgmtTile.hh"
#include "sim/clock_domain.hh"
#include "sim/clocked_object.hh"
#include "src/gw100/mgmt/crc/mgmt_crc.hh"
#include "src/gw100/mgmt/mailbox/mgmt_mailbox.hh"
#include "src/gw100/mgmt/regs/mgmt_local_regs.hh"
#include "src/gw100/mgmt/sram/mgmt_sram.hh"
#include "src/gw100/mgmt/wdt/mgmt_wdt.hh"
#include "src/gw100/ml/mgmt_link.hh"

namespace gem5
{
namespace gw100
{

/** 管理 Tile */
class Gw100MgmtTile : public ClockedObject, public MlTargetIf
{
  public:
    PARAMS(Gw100MgmtTile);

    /** 按参数构造各组件 */
    explicit Gw100MgmtTile(const Params &p);

    /** 返回名为 if_name 的端口：pio、wdt_fatal */
    Port &getPort(const std::string &if_name,
                  PortID idx = InvalidPortID) override;

    /** 接入 ML 的管理侧，开始 SRAM 的硬件清零 */
    void init() override;

    /** 撤销 ML 管理侧的复位 */
    void startup() override;

    /** ML 管理侧的目标：处理 HIF 发来的请求（spec-mgmt-tile §5.1） */
    void mlRequest(const MlTxnPtr &txn, MlRespond respond) override;

  private:
    /** 本地总线上的响应端口 */
    class PioPort : public QueuedResponsePort
    {
      public:
        /** 构造：tile 为所属的管理 Tile */
        PioPort(const std::string &name, Gw100MgmtTile &tile);

      protected:
        /** 时序访问：交给管理 Tile，稍后经队列返回响应 */
        bool recvTimingReq(PacketPtr pkt) override;

        /** 原子访问：只支持 ROM、SRAM 和本地寄存器 */
        Tick recvAtomic(PacketPtr pkt) override;

        /** 功能访问：用于加载 ROM 镜像和调试读取 */
        void recvFunctional(PacketPtr pkt) override;

        /** 本端口响应的地址范围 */
        AddrRangeList getAddrRanges() const override;

      private:
        /** 所属的管理 Tile */
        Gw100MgmtTile &tile;
        /** 待发出的响应 */
        RespPacketQueue queue;
    };

    /** PLIC 源编号（spec-mgmt-tile §7.2） */
    enum IrqSrc : unsigned
    {
        /** doorbell 信箱 */
        IrqDb = 1,
        /** 事件寄存器 */
        IrqEvt = 2,
        /** ML 错误 */
        IrqMl = 3,
        /** SRAM 可纠正 ECC 错误 */
        IrqCe = 4,
        /** SRAM 不可纠正 ECC 错误 */
        IrqUe = 5,
        /** SRAM 访问控制违例 */
        IrqAcl = 6,
        /** 看门狗第一级超时 */
        IrqWdt = 7,
        /** 中断源个数（含未用的源 0） */
        IrqCount = 8,
    };

    /** 处理一次时序访问，在完成时刻经端口返回响应 */
    void handleTiming(PacketPtr pkt);

    /**
     * 访问 ROM、SRAM、本地寄存器或信箱，返回完成时刻；不属于这些区域
     * 或访问非法时返回 MaxTick，并在 pkt 上标记错误
     */
    Tick accessLocal(PacketPtr pkt, Tick now);

    /** 管理 CPU 经 ML 访问 HIF 寄存器区（spec-mgmt-tile §5.2） */
    void accessHif(PacketPtr pkt);

    /** 在 when 时刻返回 pkt 的响应 */
    void respond(PacketPtr pkt, Tick when);

    /**
     * 把已经 makeResponse() 的 pkt 安排在 when 时刻发出；取指的响应
     * 先放进 fetchResps，到时由 deliverFetches() 交给端口
     */
    void sendResp(PacketPtr pkt, Tick when);

    /** 把到时的取指响应交给端口；CPU 停住后扣下，不再交付 */
    void deliverFetches();

    /** 时刻 t 的时钟沿上，看门狗看到的同步后的 mtime 样本 */
    uint64_t mtimeSample(Tick t) const;

    /** 看门狗状态变化后：更新中断，进入第二级，重新安排到期事件 */
    void wdtUpdate();

    /** 到期事件：在当前时钟沿判定一次自然到期 */
    void wdtExpire();

    /** 看门狗第二级超时：置位 wdt_fatal 信号，停住管理 CPU */
    void haltCpu();

    /** 把 pkt 标记为访问错误并在下一个时钟沿返回 */
    void respondError(PacketPtr pkt);

    /** 按各组件的状态更新 PLIC 中断源的电平 */
    void updateIrqs();

    /** ML 请求处理完毕的时刻到达：调用保存的 respond */
    void mlFinish();

    /** ROM 与 SRAM */
    MgmtSram sram;
    /** doorbell 信箱与事件寄存器 */
    MgmtMailbox mailbox;
    /** 本地寄存器 */
    MgmtLocalRegs regs;
    /** 装载区的字节数 */
    const uint64_t fwLoadMax;
    /** 管理链路 */
    Gw100MgmtLink *link;
    /** ML 管理侧适配器 */
    MlEndpoint *ml = nullptr;
    /** 管理 CPU 在 ML 管理侧的客户端编号 */
    unsigned cpuClient = 0;
    /** 接收中断的 PLIC */
    PlicBase *plic;
    /** 时钟复位控制器的寄存器 */
    MgmtCrc crc;
    /** clk_mgmt 切换到 PLL_CORE 后所用的频率档（时钟域的 perfLevel） */
    const unsigned pllPerfLevel;
    /** 管理系统的时钟域：clk_mgmt */
    SrcClockDomain *const mgmtClk;
    /**
     * 在切换完成的时刻改变管理系统时钟域频率档的事件；优先于同一时刻的
     * 应答，使 CPU 收到 CLK_SEL 的写应答时已按新频率计时
     */
    EventFunctionWrapper clkSwitchEvent;
    /** 看门狗 */
    MgmtWdt wdt;
    /** mtime 一个计数对应的 tick 数（dev.timestamp_hz） */
    const Tick tsPeriod;
    /** mtime 经同步器进入 clk_mgmt 域的延迟 */
    const Cycles tsSync;
    /** 管理 CPU；看门狗第二级超时时停住它 */
    BaseCPU *cpu;
    /** 看门狗第二级超时的专用信号，接 HIF */
    IntSourcePin<Gw100MgmtTile> wdtFatal;
    /** 看门狗第二级超时后，管理 CPU 已停住 */
    bool halted = false;
    /** 尚未交给端口的取指响应，按交付时刻排序 */
    std::multimap<Tick, PacketPtr> fetchResps;
    /** CPU 停住后扣下的取指，不再响应 */
    std::vector<PacketPtr> heldFetches;
    /** 在下一个取指响应的交付时刻调用 deliverFetches() 的事件 */
    EventFunctionWrapper fetchEvent;
    /** 在看门狗下一次可能到期的时钟沿调用 wdtExpire() 的事件 */
    EventFunctionWrapper wdtEvent;
    /** 各中断源当前已送给 PLIC 的电平 */
    std::array<bool, IrqCount> irqLevel{};
    /** 本地总线端口 */
    PioPort pio;

    /** 正在处理的 ML 请求的结果回调 */
    MlRespond mlRespond;
    /** 正在处理的 ML 请求的结果 */
    MlStatus mlStatus = MlStatus::Ok;
    /** 正在处理的 ML 请求读到的数据 */
    MlData mlData{};
    /** 在 ML 请求处理完毕的时刻调用 mlFinish() 的事件 */
    EventFunctionWrapper mlEvent;
};

} // namespace gw100
} // namespace gem5

#endif // __GW100_MGMT_MGMT_TILE_HH__
