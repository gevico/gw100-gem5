/**
 * @file  mgmt_tile.hh
 * @brief 管理 Tile 的外壳：本地总线端口、ML 管理侧目标、中断
 *
 * 本类负责与 gem5 的对接：pio 端口的时序、功能访问（加载 ROM 镜像），
 * 以及 PLIC 中断源的电平。各项行为由组件实现：
 * - ROM 与 SRAM：MgmtSram；
 * - 看门狗的状态机：MgmtWdt；本类按 mtime 安排到期事件，第二级超时时
 *   停住管理 CPU 并置位 wdt_fatal 信号；
 * - 管理 CPU 的停机与复位：经中断控制器 MgmtInterrupts 让 CPU 在指令
 *   边界停住，停住后去取 ROM 末尾保留的字，本类扣下这次取指；
 * - 设备完全复位（CRC 的复位时序）：收到 PCIe 设备的 dev_reset_req
 *   后复位本 Tile、ML 的两端和 PCIe 的功能逻辑（pcie_reset），释放后
 *   用复位向量处的指令响应扣下的取指，CPU 从启动 ROM 重新开始；
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
#include <set>

#include "cpu/base.hh"
#include "dev/intpin.hh"
#include "dev/riscv/plic.hh"
#include "mem/qport.hh"
#include "params/Gw100MgmtTile.hh"
#include "sim/clock_domain.hh"
#include "sim/clocked_object.hh"
#include "sim/signal.hh"
#include "src/gw100/mgmt/crc/mgmt_crc.hh"
#include "src/gw100/mgmt/intr/mgmt_interrupts.hh"
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

    /** 返回名为 if_name 的端口：pio、wdt_fatal、dev_reset_req、pcie_reset */
    Port &getPort(const std::string &if_name,
                  PortID idx = InvalidPortID) override;

    /** 接入 ML 的管理侧，开始 SRAM 的硬件清零 */
    void init() override;

    /** 撤销 ML 管理侧的复位 */
    void startup() override;

    /** ML 管理侧的目标：处理 HIF 发来的请求（spec-mgmt-tile §5.1） */
    void mlRequest(const MlTxnPtr &txn, MlRespond respond) override;

    /** dev_reset_req 置位：经同步器后开始设备完全复位 */
    void raiseInterruptPin(int num);

    /** dev_reset_req 撤销：PCIe 的功能逻辑复位时撤销，无动作 */
    void lowerInterruptPin(int num);

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

    /** 时刻 t 的时钟沿上，看门狗看到的同步后的 mtime 样本 */
    uint64_t mtimeSample(Tick t) const;

    /** 看门狗状态变化后：更新中断，进入第二级，重新安排到期事件 */
    void wdtUpdate();

    /** 到期事件：在当前时钟沿判定一次自然到期 */
    void wdtExpire();

    /** 看门狗第二级超时：置位 wdt_fatal 信号，停住管理 CPU */
    void haltCpu();

    /** 请求管理 CPU 在下一个指令边界停住（正在 wfi 的 CPU 先唤醒） */
    void requestCpuHalt();

    /**
     * 设备完全复位开始（spec-clock-reset-power §4.3）：复位 PCIe 的
     * 功能逻辑、ML 的两端和本 Tile，CPU 停住，时钟回到 XTAL
     */
    void devResetAssert();

    /** 设备完全复位释放：SRAM 开始硬件清零，ML 重新建立，CPU 重新开始 */
    void devResetRelease();

    /**
     * 复位 CPU 的状态，用复位向量处的指令响应扣下的取指，CPU 从启动
     * ROM 重新开始
     */
    void restartCpu();

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
    /** 看门狗已进入第二级，管理 CPU 已请求停住 */
    bool halted = false;
    /** 管理 CPU 的中断控制器（停机请求经它送给 CPU） */
    MgmtInterrupts *intr = nullptr;
    /** CPU 停住后去取指的地址：启动 ROM 末尾保留的字 */
    Addr haltPc = 0;
    /** CPU 停住后扣下的取指 */
    PacketPtr heldFetch = nullptr;
    /** 复位已释放，等 CPU 停住后重新开始 */
    bool restartPending = false;
    /** CPU 经 ML 读 HIF 寄存器区、尚未完成的访问 */
    std::set<PacketPtr> hifReads;
    /** 设备完全复位的请求（来自 PCIe 设备） */
    IntSinkPin<Gw100MgmtTile> devResetReq;
    /** 送给 PCIe 设备的功能逻辑复位信号 */
    SignalSourcePort<bool> pcieReset;
    /** DEV_RESET 请求经同步器进入 clk_ao 域的延迟 */
    const Tick devResetSync;
    /** 设备完全复位的复位保持时间 */
    const Tick devResetHold;
    /** clk_mgmt 选择 XTAL 时，管理系统时钟域所用的频率档 */
    const unsigned xtalPerfLevel;
    /** 设备完全复位进行中 */
    bool inDevReset = false;
    /** 开始设备完全复位的事件 */
    EventFunctionWrapper devResetAssertEvent;
    /** 释放设备完全复位的事件 */
    EventFunctionWrapper devResetReleaseEvent;
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
