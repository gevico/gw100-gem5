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
#include "src/gw100/pcie/config/capability/msix_capability.hh"
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
/** MSI-X capability 的偏移（PCI Express capability 占 0x50 – 0x8B） */
constexpr Addr MsixCapBase = 0x90;

/** DEV_CAPS 的值：窗口数、MSI-X 表项数、上下文数、DMA 通道数 */
uint32_t
devCaps(const Gw100PciDeviceParams &p)
{
    return (p.bar2_window_count & 0xFF) |
           (uint32_t(p.msix_table_size) << 8) |
           (uint32_t(p.hdi_max_contexts) << 16) |
           (uint32_t(p.hdma_channels) << 24);
}

/** HDMA 的参数（spec-host-dma §12、spec-pcie §10.3） */
HdmaEngine::Config
hdmaConfig(const Gw100PciDeviceParams &p)
{
    HdmaEngine::Config c;
    c.channels = p.hdma_channels;
    c.maxRingLog2 = p.hdma_max_ring_log2;
    c.maxDescBytes = p.hdma_max_desc_bytes;
    c.sglSegMax = p.hdma_sgl_segment_max;
    c.sglMaxSegments = p.hdma_sgl_max_segments;
    c.maxReadReq = p.hdma_max_read_request;
    c.maxOutstandingReads = p.hdma_max_outstanding_reads;
    c.cplBuffer = p.hdma_completion_buffer;
    c.d2hBuffer = p.hdma_d2h_buffer;
    c.devBase = p.devmem_base;
    c.devSize = p.devmem_size;
    c.devicePaBits = p.device_pa_bits;
    c.cplTimeout = p.pcie_cpl_timeout;
    c.quarantine = p.pcie_stale_tag_quarantine;
    return c;
}

} // anonymous namespace

/**
 * 按参数构造各组件；name 是所属设备的名字，devmem_id 是访问设备
 * 内存的请求者 ID，em 用于调度定时事件。组件之间的登记与连接由
 * Gw100PcieFactory 完成
 */
PcieParts::PcieParts(const Gw100PciDeviceParams &p, const std::string &name,
                     RequestorID devmem_id, RequestorID dma_id,
                     ClockedObject &owner)
    : status(name),
      bootCtrl(name + ".boot", status, p.fw_load_max, p.flr_proc_timeout,
               owner),
      windows(p.bar2_window_count, p.bar2_window_size, p.devmem_base,
              p.devmem_size, p.device_pa_bits),
      devmem(name + ".mem_port", devmem_id, p.devmem_max_outstanding),
      bar2Devmem(name + ".bar2_mem_port", devmem_id, 16),
      global(status, bootCtrl, p.version, devCaps(p), p.link_speed,
             p.link_width, p.timestamp_hz),
      windowRegs(windows),
      boot(bootCtrl, p.fw_load_max),
      hif(boot, bootCtrl),
      bar0(status, p.pio_latency),
      bar2(bootCtrl, windows, status, p.async_memory ? bar2Devmem : devmem,
           p.bar2_latency),
      shim(p.version, devCaps(p),
           (p.link_speed & 0xF) | (uint32_t(p.link_width & 0x1F) << 4),
           p.pio_latency),
      hdmaClock(owner, [this] { hdma.tick(); }),
      hostDma(name + ".dma", dma_id, p.system->cacheLineSize(),
              p.pcie_tx_credits),
      hdma(hdmaConfig(p), hdmaClock, hostDma, devmem, hdmaMl),
      hdmaMirror(hdma),
      intCtrl(status, hostDma, p.msix_table_size, p.hdi_max_contexts),
      msixTable(intCtrl),
      msixPba(intCtrl),
      hdiRegs(hdi)
{
    for (unsigned p = 0; p < HdiCtrl::Pages; p++)
        doorbells.push_back(std::make_unique<DoorbellPage>(hdi, p));
}

/**
 * 功能级复位：把寄存器和窗口恢复为复位值；配置空间由外壳另行复位
 */
void
PcieParts::resetRegisters()
{
    // 先复位 HDI，撤销 HDI_FATAL、HDI_CTX_ERR 对 ERROR 的贡献，否则
    // INT_STATUS 复位后 ERROR 会随即重新置位
    hdi.reset();
    status.reset();
    global.reset();
    windowRegs.reset();
    boot.reset();
    windows.reset();
    hdma.reset();
    intCtrl.reset();
}

/**
 * 设备完全复位：在 resetRegisters() 之外，再复位 FLR 保留的启动
 * 状态、FW_VERSION 和 DEV_FATAL；ML 由外壳另行复位
 */
void
PcieParts::devReset()
{
    // 先撤销 DEV_FATAL，否则 INT_STATUS 复位后 ERROR 会随即重新置位
    bootCtrl.devReset();
    boot.devReset();
    resetRegisters();
    hdma.setHold(false);
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
Gw100PcieFactory::build(const Gw100PciDeviceParams &p, ClockedObject &owner)
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
        p, name, p.system->getRequestorId(&owner, "devmem"),
        p.system->getRequestorId(&owner, "hdma"), owner);

    // 配置空间：capability 链表 PM → PCI Express（spec-pcie §4.2）
    parts->config.addCapability(PmCapBase, std::make_unique<PmCapability>());
    parts->config.addCapability(
        PcieCapBase,
        std::make_unique<PcieCapability>(p.link_speed, p.link_width));
    parts->config.addCapability(
        MsixCapBase,
        std::make_unique<MsixCapability>(p.msix_table_size, MsixTable::Base,
                                         MsixPba::Base));
    parts->config.finalize();

    // BAR0 的寄存器块（spec-pcie §5.1）；其余块在后续阶段登记
    parts->bar0.addBlock(parts->global);
    parts->bar0.addBlock(parts->windowRegs);
    parts->bar0.addBlock(parts->boot);
    parts->bar0.addBlock(parts->hdmaMirror);
    parts->bar0.addBlock(parts->msixTable);
    parts->bar0.addBlock(parts->msixPba);
    parts->bar0.addBlock(parts->hdiRegs);
    for (auto &db : parts->doorbells)
        parts->bar0.addBlock(*db);

    // HDMA：寄存器经 HIF 区，DMA 错误记入 ERR_STATUS，端口空闲时唤醒
    PcieParts *pp = parts.get();
    pp->hif.bindHdma(&pp->hdma);
    pp->hdma.onHostError = [pp] {
        pp->status.raiseErr(StatusUnit::ErrDmaHost);
    };
    pp->devmem.onReady = [pp] { pp->hdma.wake(); };
    pp->hostDma.onTxFree = [pp] {
        pp->hdma.wake();
        pp->intCtrl.trySend();
    };
    pp->hif.bindInt(&pp->intCtrl);
    pp->hif.bindHdi(&pp->hdi, &pp->status);
    pp->hdi.onHdiWr = [pp] { pp->bootCtrl.hostEvent(1u << 2); };  // HDI_WR
    pp->hdi.onErrLevel = [pp](bool e) { pp->status.setHdiErr(e); };

    parts->bars[0] = &parts->bar0;
    parts->bars[2] = &parts->bar2;
    return parts;
}

} // namespace gw100
} // namespace gem5
