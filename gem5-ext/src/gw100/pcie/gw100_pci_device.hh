/**
 * @file  gw100_pci_device.hh
 * @brief GW100 PCIe endpoint 的寄存器层
 *
 * 按 spec-pcie 实现：
 * - BAR0（1 MiB）：全局寄存器（0x0000）、BAR2 窗口配置（0x1000）、
 *   启动寄存器（0x4000，暂定的桩）；访问规则统一在前端检查，
 *   一律以成功完成结束，错误记录在 ERR_STATUS。MSI-X、HDI、DMA 镜像
 *   和 doorbell 所在的块在后续阶段实现，目前按保留处理。
 * - BAR2（64 MiB，64 位可预取）：16 个可重映射窗口，只能指向设备内存，
 *   经 mem_port 以功能访问读写，返回固定延迟（理想化）。
 * - 配置空间 0x40 起的 capability（PM、PCI Express）由本类提供：
 *   gem5 的 PciDevice 不暴露 0x40 以上的内容，也不支持扩展配置空间。
 * - FLR 与 D3hot→D0：复位配置空间、BAR 和全部寄存器，设备内存内容保留。
 *
 * @see spec-pcie
 */

#ifndef __GW100_PCIE_GW100_PCI_DEVICE_HH__
#define __GW100_PCIE_GW100_PCI_DEVICE_HH__

#include <array>
#include <cstdint>
#include <vector>

#include "dev/pci/device.hh"
#include "dev/reg_bank.hh"
#include "mem/port.hh"
#include "params/Gw100PciDevice.hh"
#include "params/Gw100PrefetchMemBar.hh"

namespace gem5
{
namespace gw100
{

/** 可预取的内存 BAR：在 PciMemBar 的写回值中置 bit 3 */
class Gw100PrefetchMemBar : public PciMemBar
{
  public:
    /** 按参数构造；大小等检查由 PciMemBar 完成 */
    Gw100PrefetchMemBar(const Gw100PrefetchMemBarParams &p) : PciMemBar(p) {}

    /**
     * 写 BAR 寄存器：交给 PciMemBar 处理后在写回值中置 bit 3
     * （Prefetchable）；bit 3 是硬件固定位，不参与地址计算
     */
    uint32_t write(const PciUpstream::DeviceInterface &interface,
                   uint32_t val) override;
};

/** GW100 的 PCIe endpoint：寄存器层 */
class Gw100PciDevice : public PciEndpoint
{
  public:
    PARAMS(Gw100PciDevice);

    /** 按参数初始化寄存器视图、全局与启动寄存器组和 capability */
    explicit Gw100PciDevice(const Params &p);

    /**
     * 返回名为 if_name 的端口：mem_port 是设备内存端口，其余交给
     * PciEndpoint
     */
    Port &getPort(const std::string &if_name,
                  PortID idx = InvalidPortID) override;

    /**
     * 保存寄存器状态、BAR2 窗口和配置空间 0x40 以上的内容；配置空间
     * 头部由基类保存
     */
    void serialize(CheckpointOut &cp) const override;

    /** 从 checkpoint 恢复 serialize() 保存的状态 */
    void unserialize(CheckpointIn &cp) override;

  protected:
    /** BAR 读访问的入口：按 BAR 号分派到 accessBar0() 或 accessBar2() */
    Tick readDevice(PacketPtr pkt) override;

    /** BAR 写访问的入口：按 BAR 号分派到 accessBar0() 或 accessBar2() */
    Tick writeDevice(PacketPtr pkt) override;

    /** 配置读：0x40 以下交给基类，0x40 起由本类的 capability 提供 */
    Tick readConfig(PacketPtr pkt) override;

    /**
     * 配置写：0x40 以下交给基类；0x40 起按可写位和写 1 清零位更新，
     * 并处理 FLR 与 D3hot→D0
     */
    Tick writeConfig(PacketPtr pkt) override;

  private:
    /** 接设备内存的端口；本阶段只做功能访问 */
    class MemPort : public RequestPort
    {
      public:
        /** 构造名为 name 的端口 */
        MemPort(const std::string &name) : RequestPort(name) {}

      protected:
        /** 本阶段不发 timing 请求，收到响应说明用法错误 */
        bool recvTimingResp(PacketPtr) override
        { panic("Gw100PciDevice::MemPort 只支持功能访问"); }

        /** 本阶段不发 timing 请求，收到重试说明用法错误 */
        void recvReqRetry() override
        { panic("Gw100PciDevice::MemPort 只支持功能访问"); }
    };

    /** ERR_STATUS 的位（spec-pcie §6.3） */
    enum : uint32_t
    {
        /** 访问了 BAR0 的保留偏移 */
        ErrRsvdAccess = 1u << 0,
        /** 以非法宽度访问寄存器 */
        ErrBadWidth = 1u << 1,
        /** 访问了未使能的 BAR2 窗口 */
        ErrWinDisabled = 1u << 2,
        /** BAR2 窗口的目标不在设备内存内，或写了只读窗口 */
        ErrWinTarget = 1u << 3,
        /** 写入 doorbell 的值越界（命令协议实现后使用） */
        ErrDbInvalid = 1u << 4,
        /** DMA 访问 host 内存出错（DMA 实现后使用） */
        ErrDmaHost = 1u << 5,
        /** 收到 poisoned TLP（链路建模后使用） */
        ErrRxPoisoned = 1u << 6,
        /** 启动窗口收到非法写入（启动过程建模后使用） */
        ErrBootWin = 1u << 7,
    };

    /** INT_STATUS 的位（spec-pcie §8.4） */
    enum : uint32_t
    {
        /** 管理完成队列有新的完成记录 */
        IntAdmin = 1u << 0,
        /** ERR_STATUS 非零等错误条件 */
        IntError = 1u << 1,
        /** 固件事件 */
        IntFwEvent = 1u << 2,
    };

    /** 一个 BAR2 窗口的配置（spec-pcie §7.2） */
    struct Window
    {
        /** WIN_CTRL：EN、RD_ONLY 两位 */
        uint32_t ctrl = 0;
        /** 目标设备物理地址，按窗口大小对齐 */
        uint64_t base = 0;
    };

    /** BAR0 中每个寄存器块的大小 */
    static constexpr Addr Bar0BlockSize = 0x1000;
    /** 全局寄存器块的起始偏移 */
    static constexpr Addr GlobalBlock = 0x0000;
    /** BAR2 窗口配置块的起始偏移 */
    static constexpr Addr WindowBlock = 0x1000;
    /** 启动寄存器块的起始偏移 */
    static constexpr Addr BootBlock = 0x4000;
    /** 相邻两个窗口配置之间的间隔 */
    static constexpr Addr WindowStride = 0x10;
    /** WIN_CTRL.EN：窗口使能 */
    static constexpr uint32_t WinCtrlEn = 1u << 0;
    /** WIN_CTRL.RD_ONLY：窗口只读 */
    static constexpr uint32_t WinCtrlRdOnly = 1u << 1;

    /** 配置空间（含扩展部分）的大小 */
    static constexpr Addr CfgSize = 0x1000;
    /** 从这个偏移起的配置空间由本类提供 */
    static constexpr Addr CfgOwnBase = 0x40;
    /** PM capability 的偏移 */
    static constexpr Addr PmCap = 0x40;
    /** PCI Express capability 的偏移 */
    static constexpr Addr PcieCap = 0x50;
    /** PCI Express capability 中 Device Control 的偏移 */
    static constexpr Addr PcieDevCtl = PcieCap + 0x08;
    /** Device Control 中的 Initiate FLR 位 */
    static constexpr uint16_t DevCtlInitiateFlr = 1u << 15;
    /** PM capability 中 PMCSR 的偏移 */
    static constexpr Addr PmCsr = PmCap + 0x04;

    /**
     * 构造 PM 和 PCI Express capability 的初值、可写位和写 1 清零位，
     * 并保存为复位值
     */
    void buildCapabilities();

    /** 把 BAR0 寄存器状态和全部 BAR2 窗口恢复为复位值 */
    void resetRegisters();

    /**
     * 功能级复位（FLR、D3hot→D0）：恢复 capability 和寄存器，清零
     * Command 和全部 BAR；设备内存内容保留
     */
    void functionReset(const char *why);

    /**
     * 构造一次配置写交给 PciEndpoint，用于复位配置空间头部（Command、
     * BAR），使 BAR 对象的状态一并更新
     */
    void cfgWriteCommon(Addr off, unsigned size, uint32_t val);

    /**
     * BAR0 访问：先按 spec-pcie §5.2 检查宽度和偏移，出错时记录并以
     * 成功完成结束；合法访问分派到寄存器组或窗口配置
     */
    Tick accessBar0(PacketPtr pkt, Addr off, bool is_write);

    /**
     * BAR2 访问：按窗口换算成设备物理地址，检查使能、目标范围和只读后
     * 经 mem_port 功能访问设备内存
     */
    Tick accessBar2(PacketPtr pkt, Addr off, bool is_write);

    /** [off, off + size) 覆盖的每个 32 位字是否都是已实现的寄存器 */
    bool bar0Implemented(Addr off, unsigned size) const;

    /**
     * 置 ERR_STATUS 的 bit；ERR_STATUS 原为 0 时锁存这次错误的偏移和
     * 访问信息
     */
    void logError(uint32_t bit, Addr off, bool is_write, unsigned size,
                  unsigned bar);

    /** 错误条件仍然成立时置 INT_STATUS.ERROR */
    void updateIntStatus();

    /** 当前设备时间戳：仿真时间换算到 timestamp_hz */
    uint64_t timestamp() const;

    /** DEV_VERSION 的值 */
    const uint32_t version;
    /** DEV_CAPS 的值：窗口数、MSI-X 表项数、上下文数、DMA 通道数 */
    const uint32_t devCaps;
    /** BAR2 窗口个数 */
    const unsigned winCount;
    /** 每个 BAR2 窗口的大小 */
    const Addr winSize;
    /** 经 BAR2 访问设备内存的固定延迟 */
    const Tick bar2Latency;
    /** 设备内存的起始设备物理地址 */
    const Addr devmemBase;
    /** 设备内存大小 */
    const Addr devmemSize;
    /** 设备物理地址宽度 */
    const unsigned devicePaBits;
    /** 设备时间戳频率，Hz */
    const uint32_t timestampHz;
    /** 当前链路速率：1 = Gen1 … 4 = Gen4 */
    const uint8_t linkSpeed;
    /** 当前链路宽度 */
    const uint8_t linkWidth;

    /** 接设备内存的端口 */
    MemPort memPort;
    /** 设备访问设备内存、构造配置写时使用的请求者 ID */
    RequestorID memRequestorId;

    /** SCRATCH */
    uint32_t scratch = 0;
    /** INT_STATUS */
    uint32_t intStatus = 0;
    /** INT_MASK */
    uint32_t intMask = 0xFFFFFFFF;
    /** ERR_STATUS */
    uint32_t errStatus = 0;
    /** ERR_ADDR：第一次错误的 BAR 偏移 */
    uint32_t errAddr = 0;
    /** ERR_INFO：第一次错误的访问类型、字节数和 BAR */
    uint32_t errInfo = 0;
    /** 读 TIMESTAMP_LO 时锁存的高 32 位，供 TIMESTAMP_HI 返回 */
    uint32_t tsLatchedHi = 0;
    /** FW_SIZE */
    uint32_t fwSize = 0;
    /** 全部 BAR2 窗口的配置 */
    std::vector<Window> windows;

    /** 配置空间 0x40–0xFFF 的当前值 */
    std::array<uint8_t, CfgSize> cfg{};
    /** 配置空间 0x40–0xFFF 的复位值 */
    std::array<uint8_t, CfgSize> cfgReset{};
    /** 配置空间 0x40–0xFFF 中可写的位 */
    std::array<uint8_t, CfgSize> cfgWmask{};
    /** 配置空间 0x40–0xFFF 中写 1 清零的位 */
    std::array<uint8_t, CfgSize> cfgW1c{};

    /**
     * 全局寄存器组（spec-pcie §6.1）；寄存器对象只是上面状态变量的
     * 视图
     */
    RegisterBankLE globalBank{"global", GlobalBlock};
    /** DEV_VERSION（0x000，RO） */
    RegisterBankLE::Register32 regDevVersion{"DEV_VERSION"};
    /** DEV_CAPS（0x004，RO） */
    RegisterBankLE::Register32 regDevCaps{"DEV_CAPS"};
    /** DEV_STATUS（0x008，RO） */
    RegisterBankLE::Register32 regDevStatus{"DEV_STATUS"};
    /** SCRATCH（0x00C，RW） */
    RegisterBankLE::Register32 regScratch{"SCRATCH"};
    /** TIMESTAMP_LO（0x010，RO，读取时锁存高位） */
    RegisterBankLE::Register32 regTsLo{"TIMESTAMP_LO"};
    /** TIMESTAMP_HI（0x014，RO，返回锁存值） */
    RegisterBankLE::Register32 regTsHi{"TIMESTAMP_HI"};
    /** TIMESTAMP_FREQ（0x018，RO） */
    RegisterBankLE::Register32 regTsFreq{"TIMESTAMP_FREQ"};
    /** INT_STATUS（0x020，RW1C） */
    RegisterBankLE::Register32 regIntStatus{"INT_STATUS"};
    /** INT_MASK（0x024，RW） */
    RegisterBankLE::Register32 regIntMask{"INT_MASK"};
    /** INT_SET（0x028，W1S） */
    RegisterBankLE::Register32 regIntSet{"INT_SET"};
    /** ERR_STATUS（0x030，RW1C） */
    RegisterBankLE::Register32 regErrStatus{"ERR_STATUS"};
    /** ERR_ADDR（0x034，RO） */
    RegisterBankLE::Register32 regErrAddr{"ERR_ADDR"};
    /** ERR_INFO（0x038，RO） */
    RegisterBankLE::Register32 regErrInfo{"ERR_INFO"};

    /** 启动寄存器组（spec-pcie §6.4，暂定的桩：上电即就绪） */
    RegisterBankLE bootBank{"boot", BootBlock};
    /** BOOT_STATUS（0x4000，RO） */
    RegisterBankLE::Register32 regBootStatus{"BOOT_STATUS"};
    /** BOOT_CTRL（0x4004，W1S，本阶段写入无效果） */
    RegisterBankLE::Register32 regBootCtrl{"BOOT_CTRL"};
    /** FW_LOAD_MAX（0x4010，RO） */
    RegisterBankLE::Register32 regFwLoadMax{"FW_LOAD_MAX"};
    /** FW_SIZE（0x4014，RW；只在 STAGE 2 可写，桩中写入被忽略） */
    RegisterBankLE::Register32 regFwSize{"FW_SIZE"};
    /** FW_VERSION（0x4018，RO） */
    RegisterBankLE::Register32 regFwVersion{"FW_VERSION"};
};

} // namespace gw100
} // namespace gem5

#endif // __GW100_PCIE_GW100_PCI_DEVICE_HH__
