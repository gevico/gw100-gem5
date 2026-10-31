/**
 * @file  pcie_factory.cc
 * @brief PcieParts 与 Gw100PcieFactory 的实现
 *
 * @see spec-pcie
 */

#include "src/gw100/pcie/pcie_factory.hh"

#include "base/intmath.hh"
#include "base/logging.hh"
#include "dev/pci/device.hh"
#include "sim/system.hh"
#include "src/gw100/pcie/config/capability/pcie_capability.hh"
#include "src/gw100/pcie/config/capability/pm_capability.hh"

namespace gem5
{
namespace gw100
{

namespace
{

/** PM capability 在配置空间中的偏移（spec-pcie §4.2） */
constexpr Addr PmCapBase = 0x40;
/** PCI Express capability 在配置空间中的偏移（spec-pcie §4.2） */
constexpr Addr PcieCapBase = 0x50;

/** DEV_CAPS 的值：窗口数、MSI-X 表项数、上下文数、DMA 通道数 */
uint32_t
devCaps(const Gw100PciDeviceParams &p)
{
    return (p.bar2_window_count & 0xFF) |
           (uint32_t(p.msix_table_size) << 8) |
           (uint32_t(p.hdi_max_contexts) << 16) |
           (uint32_t(p.hdma_channels) << 24);
}

} // anonymous namespace

/**
 * 按参数构造各组件；name 是所属设备的名字，devmem_id 是访问设备
 * 内存的请求者 ID。组件之间的登记与连接由 Gw100PcieFactory 完成
 */
PcieParts::PcieParts(const Gw100PciDeviceParams &p, const std::string &name,
                     RequestorID devmem_id)
    : status(name),
      windows(p.bar2_window_count, p.bar2_window_size, p.devmem_base,
              p.devmem_size, p.device_pa_bits),
      devmem(name + ".mem_port", devmem_id),
      global(status, p.version, devCaps(p), p.link_speed, p.link_width,
             p.timestamp_hz),
      windowRegs(windows),
      bar0(status, p.pio_latency),
      bar2(windows, status, devmem, p.bar2_latency)
{}

/**
 * 功能级复位：把寄存器和窗口恢复为复位值；配置空间由外壳另行复位
 */
void
PcieParts::resetRegisters()
{
    status.reset();
    global.reset();
    windowRegs.reset();
    boot.reset();
    windows.reset();
}

/** 按组件分节保存全部状态 */
void
PcieParts::serialize(CheckpointOut &cp) const
{
    status.serializeSection(cp, "status");
    config.serializeSection(cp, "config");
    windows.serializeSection(cp, "windows");
    global.serializeSection(cp, "global");
    boot.serializeSection(cp, "boot");
}

/** 按组件分节恢复 serialize() 保存的状态 */
void
PcieParts::unserialize(CheckpointIn &cp)
{
    status.unserializeSection(cp, "status");
    config.unserializeSection(cp, "config");
    windows.unserializeSection(cp, "windows");
    global.unserializeSection(cp, "global");
    boot.unserializeSection(cp, "boot");
}

/**
 * 检查参数，创建全部组件，登记 capability 与寄存器块并互相连接；
 * owner 是所属设备，用于取名字和注册请求者 ID
 */
std::unique_ptr<PcieParts>
Gw100PcieFactory::build(const Gw100PciDeviceParams &p, SimObject &owner)
{
    const std::string &name = owner.name();
    const Addr win_size = p.bar2_window_size;
    fatal_if(!isPowerOf2(win_size), "%s: BAR2 窗口大小必须是 2 的幂", name);
    fatal_if(p.bar2_window_count == 0 || p.bar2_window_count > 0xFF,
             "%s: BAR2 窗口个数越界", name);
    fatal_if(!p.BAR2 || p.BAR2->size() != p.bar2_window_count * win_size,
             "%s: BAR2 大小必须等于窗口个数 × 窗口大小", name);
    fatal_if(p.device_pa_bits < 32 || p.device_pa_bits > 64,
             "%s: 设备物理地址宽度越界", name);

    auto parts = std::make_unique<PcieParts>(
        p, name, p.system->getRequestorId(&owner, "devmem"));

    // 配置空间：capability 链表 PM → PCI Express（spec-pcie §4.2）
    parts->config.addCapability(PmCapBase, std::make_unique<PmCapability>());
    parts->config.addCapability(
        PcieCapBase,
        std::make_unique<PcieCapability>(p.link_speed, p.link_width));
    parts->config.finalize();

    // BAR0 的寄存器块（spec-pcie §5.1）；其余块在后续阶段登记
    parts->bar0.addBlock(parts->global);
    parts->bar0.addBlock(parts->windowRegs);
    parts->bar0.addBlock(parts->boot);

    parts->bars[0] = &parts->bar0;
    parts->bars[2] = &parts->bar2;
    return parts;
}

} // namespace gw100
} // namespace gem5
