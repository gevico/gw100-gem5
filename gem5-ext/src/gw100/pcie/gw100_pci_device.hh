/**
 * @file  gw100_pci_device.hh
 * @brief GW100 PCIe endpoint 的外壳
 *
 * 本类只负责与 gem5 的对接：PciEndpoint 的 BAR 与配置访问入口、
 * mem_port、功能级复位的编排和 checkpoint。各项行为由 Gw100PcieFactory
 * 组装的组件实现（见 pcie_factory.hh）：
 * - 配置空间 0x40 起：ConfigSpace 与各 Capability；
 * - BAR0：Bar0Handler 与 GlobalRegs、WindowRegs、BootRegs；
 * - BAR2：Bar2Handler、WindowTable、DevmemBackend；
 * - ERR/INT 状态：StatusUnit；
 * - Host DMA：HdmaEngine，经 dma 端口访问 host 内存，经 mem_port 访问
 *   设备内存，寄存器经 HIF 区，BAR0 0x6000 是它的只读镜像；
 * - 中断：IntCtrl，MSI-X 消息与 DMA 写共用 dma 端口，INTA 经
 *   PciEndpoint 的 intrPost/intrClear。
 * 外壳还接收看门狗第二级超时的专用信号 wdt_fatal（spec-mgmt-tile
 * §7.4），经两级同步器后置位 DEV_FATAL；host 写 DEV_RESET 时进入
 * reset shim 并经 dev_reset_req 请求 CRC 复位，由 CRC 经 func_reset
 * 复位功能逻辑（spec-clock-reset-power §4.3）。
 *
 * @see spec-pcie
 */

#ifndef __GW100_PCIE_GW100_PCI_DEVICE_HH__
#define __GW100_PCIE_GW100_PCI_DEVICE_HH__

#include <cstdint>
#include <deque>
#include <memory>

#include <fstream>
#include "base/statistics.hh"
#include "dev/intpin.hh"
#include "dev/pci/device.hh"
#include "params/Gw100PciDevice.hh"
#include "sim/signal.hh"
#include "src/gw100/ml/mgmt_link.hh"
#include "src/gw100/pcie/link/link_transport.hh"
#include "src/gw100/pcie/pcie_factory.hh"

namespace gem5
{
namespace gw100
{

class Gw100MemoryIngress;

/** GW100 的 PCIe endpoint：组件的外壳 */
class Gw100PciDevice : public PciEndpoint, public pcie::LinkClockIf
{
  public:
    PARAMS(Gw100PciDevice);

    /** 按参数构造；全部组件由 Gw100PcieFactory 组装 */
    explicit Gw100PciDevice(const Params &p);

    /**
     * 返回名为 if_name 的端口：dma 是 HDMA 访问 host 内存的端口，
     * mem_port 是设备内存端口，wdt_fatal 是看门狗第二级超时的信号，
     * dev_reset_req、func_reset 是设备完全复位的请求与复位信号，其余
     * 交给 PciEndpoint
     */
    Port &getPort(const std::string &if_name,
                  PortID idx = InvalidPortID) override;

    /** 检查端口，接入 ML 的 HIF 侧（接了管理链路时） */
    void init() override;

    /** 撤销 ML HIF 侧的复位（接了管理链路时） */
    void startup() override;
    /** 精确配置使用异步 PIO 入口 */
    bool
    usesTimingPio() const override
    { return asyncMemory; }
    /** 接受有界的异步 PIO 请求 */
    bool timingPio(PacketPtr pkt) override;
    /** 上游撤销异步 PIO 响应反压 */
    void timingPioRetry() override;

    /** 保存配置空间头部（基类）和各组件的状态 */
    void serialize(CheckpointOut &cp) const override;

    /** 从 checkpoint 恢复 serialize() 保存的状态 */
    void unserialize(CheckpointIn &cp) override;

    /** wdt_fatal 信号置位：经两级同步器后置位 DEV_FATAL */
    void raiseInterruptPin(int num);

    /** wdt_fatal 信号撤销：它保持到复位，不会撤销 */
    void lowerInterruptPin(int num);
    /** 按 PERST=2、Hot Reset=4、Link Down=8 重建配置和链路。 */
    void resetLink(Tick origin, unsigned cause);
    /** 链路内核的 1 ps 时钟。 */
    uint64_t
    now() const override
    {
        return curTick();
    }
    /** 在指定协议边界运行共享链路事件。 */
    void wakeAt(uint64_t when) override;
    /** 排空 Host、共享链路与目标完成。 */
    DrainState drain() override;
    /** checkpoint 恢复后继续协议维护。 */
    void drainResume() override;
    /** 返回实际链路配置、有限资源与计数的 JSON 快照。 */
    std::string pcieReport();

  protected:
    /** BAR 读访问的入口：交给该 BAR 的处理者 */
    Tick readDevice(PacketPtr pkt) override;

    /** BAR 写访问的入口：交给该 BAR 的处理者 */
    Tick writeDevice(PacketPtr pkt) override;

    /** 配置读：0x40 以下交给基类，0x40 起交给 ConfigSpace */
    Tick readConfig(PacketPtr pkt) override;

    /**
     * 配置写：0x40 以下交给基类；0x40 起交给 ConfigSpace，并按返回的
     * 事件处理 FLR 与 D3hot→D0
     */
    Tick writeConfig(PacketPtr pkt) override;

  private:
    /** 把一次 BAR 访问交给对应 BAR 的处理者，并完成响应 */
    Tick accessBar(PacketPtr pkt, bool is_write);

    /**
     * 功能级复位（FLR、D3hot→D0）：恢复配置空间和寄存器，清零
     * Command 和全部 BAR；设备内存内容保留
     */
    void functionReset(const char *why);

    /**
     * 构造一次配置写交给 PciEndpoint，用于复位配置空间头部（Command、
     * BAR），使 BAR 对象的状态一并更新
     */
    void cfgWriteCommon(Addr off, unsigned size, uint32_t val);

    /** PCI_STATE（spec-pcie §6.5）：配置空间中固件需要的状态 */
    uint32_t pciState();

    /** host 写 DEV_RESET：进入 reset shim，请求 CRC 复位 */
    void devResetRequest();

    /** 按 Device Control 的 MPS、MRRS、Extended Tag 更新 HDMA */
    void updateHdmaPcie();

    /**
     * Command 寄存器变化：Bus Master Enable 通知 HDMA 与中断控制器，由 1
     * 变 0 时发出 BME_OFF 事件；Interrupt Disable 通知中断控制器
     */
    void updateBusMaster();

    /** MSI-X capability 的 Enable、Function Mask 变化：通知中断控制器 */
    void updateMsix();

    /**
     * CRC 的复位信号变化：asserted 时复位功能逻辑和 ML 的 HIF 侧，
     * 撤销时释放 ML 并退出 reset shim
     */
    void funcReset(bool asserted);

    /** 全部组件 */
    std::unique_ptr<PcieParts> parts;
    /** 原生 PCIe 配置头部的上电值 */
    PCIConfigType0 resetConfig;
    /** 管理链路；为空时不接管理 Tile */
    Gw100MgmtLink *link;
    /** ML 的 HIF 侧适配器 */
    MlEndpoint *ml = nullptr;
    /** 看门狗第二级超时的专用信号（来自管理 Tile） */
    IntSinkPin<Gw100PciDevice> wdtFatalPin;
    /** 信号经两级同步器到达 clk_pcie 域时置位 DEV_FATAL 的事件 */
    EventFunctionWrapper wdtFatalEvent;
    /** 设备完全复位的请求，送给 CRC（管理 Tile） */
    IntSourcePin<Gw100PciDevice> devResetReqPin;
    /** CRC 送来的功能逻辑复位信号：true 为复位中 */
    SignalSinkPort<bool> funcResetPin;
    /** 上一次看到的 Bus Master Enable */
    bool lastBme = false;
    /** doorbell 在 ML HIF 侧的客户端编号 */
    unsigned dbClient = 0;
    /** 一次保持到响应被接受的 PIO */
    struct PendingPio
    {
        /** 原始请求包 */
        PacketPtr packet = nullptr;
        /** 接受时的功能代次 */
        uint64_t epoch = 0;
        /** 最早执行或返回时刻 */
        Tick ready = 0;
        /** 已交给处理者 */
        bool started = false;
        /** 处理者已给出结果 */
        bool done = false;
        /** 复位已作废此请求 */
        bool cancelled = false;
        /** 精确链路目标完成回调，空值使用兼容 PIO 返回。 */
        std::function<void(PacketPtr)> linkDone;
    };
    /** 处理异步 PIO 和响应 */
    void processPio();
    /** 安排异步 PIO 生效 */
    void wakePio(Tick when);
    /** 作废 PCIe 旧内存事务 */
    void invalidateMemory();
    /** 是否使用精确异步路径 */
    const bool asyncMemory;
    /** 两个 PCIe 内存来源 */
    const std::vector<Gw100MemoryIngress *> memoryIngresses;
    /** 当前 PCIe 功能代次 */
    uint64_t pioEpoch = 0;
    /** 已接受的 PIO，最多十六项 */
    std::deque<std::shared_ptr<PendingPio>> pendingPio;
    /** 需要通知请求重试 */
    bool pioRequestRetry = false;
    /** 返回路径正在反压 */
    bool pioResponseBlocked = false;
    /** 异步 PIO 调度事件 */
    EventFunctionWrapper pioEvent;
    /** 一个已由 RC 本地入口接受的原生 Host 请求。 */
    struct HostIngress
    {
        /** 原生请求，保留到上游响应被接受。 */
        PacketPtr packet = nullptr;
        /** 从硬件入口构造的线上事务。 */
        pcie::Tlp tlp;
        /** Host 总线传输到达 RC 的时刻。 */
        Tick ready = 0;
        /** DLL 不可用时 RC 本地保留请求的上界，单位 ps。 */
        Tick linkDeadline = 0;
    };
    /** 处理共享链路、Host 准入与返回。 */
    void processLink();
    /** 将线上请求交给 EP PIO 或 RC Host timing 目标。 */
    bool executeLink(unsigned side, const pcie::Tlp &tlp,
                     pcie::TargetDone done);
    /** Host PIO 最终完成，返回队列保留其全部反压占用。 */
    void finishHost(PacketPtr pkt, HostStatus status, const uint8_t *data);
    /** 实际数字链路状态更新配置空间。 */
    void updateLinkStatus();
    /** 将一次线上错误归入 AER 与错误消息。 */
    void linkError(unsigned side, pcie::LinkError error,
                   const std::vector<uint8_t> &header);
    /** 保存已排空的协议状态、事务层门控和统计。 */
    void serializeLink(CheckpointOut &cp) const;
    /** 恢复已排空的协议状态、事务层门控和统计。 */
    void unserializeLink(CheckpointIn &cp);
    /** 记录原始链路观测和所在系统时刻。 */
    void traceLink(const pcie::LinkTrace &trace);
    /** 为原生目标开始或完成建立可分项统计的观测。 */
    void traceService(unsigned side, uint64_t cookie, bool complete);
    /** PCIe 的双向计数与累计服务时间。 */
    struct LinkStats : public statistics::Group
    {
        /** 建立 RC、EP 两项向量。 */
        explicit LinkStats(statistics::Group *parent);
        /** 本地接受的 TLP 数。 */
        statistics::Vector accepted;
        /** 首次发送的 TLP 数。 */
        statistics::Vector transmitted;
        /** 首次发送的有效载荷，单位 B。 */
        statistics::Vector payload;
        /** 重放次数。 */
        statistics::Vector replays;
        /** DLLP 发送数。 */
        statistics::Vector dllps;
        /** 线上错误数。 */
        statistics::Vector errors;
        /** 包串行占用，单位 tick。 */
        statistics::Vector packetTicks;
        /** SKP 及 framing 占用，单位 tick。 */
        statistics::Vector maintenanceTicks;
        /** 训练占用，单位 tick。 */
        statistics::Vector trainingTicks;
        /** 接受到 DLL 提交的累计排队时间，单位 tick。 */
        statistics::Vector queueTicks;
        /** 接受到完整 completion 的累计时间，单位 tick。 */
        statistics::Vector requestTicks;
        /** 请求者收到全部 completion 的次数。 */
        statistics::Vector completed;
        /** 原生目标服务累计时间，单位 tick。 */
        statistics::Vector targetTicks;
    };
    /** 与实际链路事件同步的统计。 */
    LinkStats linkStats;
    /** 一个有限的源端观测条目。 */
    struct ObservedRequest
    {
        /** 本地接受时刻。 */
        Tick accepted = 0;
        /** 用于区分 posted/NP/completion 的实体类别。 */
        pcie::Traffic traffic = pcie::Traffic::Posted;
    };
    /** 仅跟踪已有有限资源内的源端事务。 */
    std::array<std::map<uint64_t, ObservedRequest>, 2> observedRequests;
    /** 仅跟踪两端各六十四个有限原生目标。 */
    std::array<std::map<uint64_t, Tick>, 2> observedTargets;
    /** 精确配置的共享 RC/EP 事务链路。 */
    std::unique_ptr<pcie::LinkTransport> pcieTransport;
    /** RC 原生入口与返回合计最多六十四个请求。 */
    unsigned hostRequests = 0;
    /** 等待 Host 总线到达与共享链路准入的完整操作。 */
    std::deque<HostIngress> hostIngress;
    /** 等待上游接受的已完成原生请求。 */
    std::deque<PacketPtr> hostResponses;
    /** 共享链路事件，复位和门控先于本事件生效。 */
    EventFunctionWrapper linkEvent;
    /** 可选原始线上事件文件。 */
    std::ofstream linkTrace;
    /** 全系统要求排空，关闭新的原生 Host 准入。 */
    bool linkDraining = false;
    /** 原生入口已有请求均进入链路后，已经启动内核排空。 */
    bool linkDrainStarted = false;
};

} // namespace gw100
} // namespace gem5

#endif // __GW100_PCIE_GW100_PCI_DEVICE_HH__
