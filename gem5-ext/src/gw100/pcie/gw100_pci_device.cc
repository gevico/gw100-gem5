/**
 * @file  gw100_pci_device.cc
 * @brief Gw100PciDevice 的实现
 *
 * @see spec-pcie
 */

#include "src/gw100/pcie/gw100_pci_device.hh"
#include "src/gw100/mem_tile/memory_ingress.hh"

#include <algorithm>
#include <cstring>

#include "base/trace.hh"
#include "debug/Gw100Pcie.hh"
#include "dev/pci/pcireg.h"
#include "mem/packet_access.hh"
#include "src/gw100/pcie/config/capability/msix_capability.hh"

namespace gem5
{
namespace gw100
{

namespace
{

/** Device Control 在配置空间中的偏移：PCI Express capability（0x50）+ 8 */
constexpr Addr DevCtl = 0x58;
/** MSI-X 的 Message Control：MSI-X capability（0x90）+ 2 */
constexpr Addr MsixCtl = 0x92;
/** PM capability 的电源控制与状态寄存器 */
constexpr Addr PmCsr = 0x44;
/** Command 的 Interrupt Disable */
constexpr uint16_t CmdIntxDisable = 0x0400;
/** HIF 事件位：BME_OFF（spec-mgmt-tile §6.3） */
constexpr uint32_t EvtBmeOff = 1u << 3;

} // anonymous namespace

/** 安排异步 PIO 生效 */
void
Gw100PciDevice::wakePio(Tick when)
{
    when = std::max(when, curTick() + 1);
    if (!pioEvent.scheduled()) {
        schedule(pioEvent, when);
    } else if (when < pioEvent.when()) {
        reschedule(pioEvent, when);
    }
}

/** 接受有界的异步 PIO 请求 */
bool
Gw100PciDevice::timingPio(PacketPtr pkt)
{
    if (pcieTransport) {
        if (linkDraining || hostRequests >= pciedefs::HOST_PACKET_SLOTS) {
            pioRequestRetry = true;
            return false;
        }
        bool config_access =
            upstreamInterface.configRange().contains(pkt->getAddr());
        Addr address = config_access
                           ? pkt->getAddr() & 4095
                           : pkt->getAddr() - upstreamInterface.memAddr(0);
        pcie::TlpKind kind =
            config_access ? (pkt->isWrite() ? pcie::TlpKind::ConfigWrite
                                            : pcie::TlpKind::ConfigRead)
                          : (pkt->isWrite() ? pcie::TlpKind::MemoryWrite
                                            : pcie::TlpKind::MemoryRead);
        auto tlp = pcie::Tlp::request(
            kind, address, pkt->getSize(), 0, 0,
            pkt->isWrite() ? pkt->getConstPtr<uint8_t>() : nullptr);
        tlp.destination = *parts->hostDma.requesterId;
        Tick ready = curTick() + pkt->headerDelay + pkt->payloadDelay;
        pkt->headerDelay = pkt->payloadDelay = 0;
        hostIngress.push_back(
            {pkt, tlp, ready, ready + pciedefs::RC_LINK_WAIT_PS});
        ++hostRequests;
        wakeAt(ready);
        return true;
    }
    if (pendingPio.size() >= 16) {
        pioRequestRetry = true;
        return false;
    }
    auto p = std::make_shared<PendingPio>();
    p->packet = pkt;
    p->epoch = pioEpoch;
    p->ready = curTick() + pkt->headerDelay + pkt->payloadDelay;
    pkt->headerDelay = pkt->payloadDelay = 0;
    pendingPio.push_back(p);
    wakePio(p->ready);
    return true;
}

/** 上游撤销异步 PIO 响应反压 */
void
Gw100PciDevice::timingPioRetry()
{
    pioResponseBlocked = false;
    if (pcieTransport) {
        wakeAt(curTick());
        return;
    }
    wakePio(curTick() + 1);
}

/** 作废 PCIe 旧内存事务 */
void
Gw100PciDevice::invalidateMemory()
{
    ++pioEpoch;
    for (auto *ingress : memoryIngresses) {
        ingress->invalidate();
    }
    if (!pendingPio.empty()) {
        wakePio(curTick() + 1);
    }
}

/** 处理异步 PIO 和响应 */
void
Gw100PciDevice::processPio()
{
    for (auto &p : pendingPio) {
        PacketPtr pkt = p->packet;
        // 已发出的异步访问持有原包直到返回，以保持回调缓冲有效。
        if (p->epoch != pioEpoch && (!p->started || p->done) &&
            !p->cancelled) {
            p->cancelled = true;
            if (pkt->isRead()) {
                std::memset(pkt->getPtr<uint8_t>(), 0xFF, pkt->getSize());
            }
            if (pkt->isRequest()) {
                pkt->makeResponse();
            }
            pkt->setBadCommand();
            p->started = p->done = true;
            p->ready = curTick();
        }
        if (p->started) {
            continue;
        }
        if (p->ready > curTick()) {
            wakePio(p->ready);
            continue;
        }
        int bar = -1;
        Addr off = 0;
        bool is_bar = getBAR(pkt->getAddr(), bar, off);
        if (is_bar && bar == 2 && !parts->shim.active()) {
            BarAccess a{off, unsigned(pkt->getSize()), pkt->isWrite(),
                        const_cast<uint8_t *>(pkt->getConstPtr<uint8_t>())};
            if (pkt->isMaskedWrite()) {
                a.byteEnable = &pkt->req->getByteEnable();
            }
            auto done = [this, p](Tick latency) {
                p->packet->makeResponse();
                p->done = true;
                p->ready = curTick() + latency;
                wakePio(p->ready);
            };
            auto valid = [this, epoch = p->epoch] {
                return epoch == pioEpoch;
            };
            p->started = true;
            if (!parts->bar2.accessAsync(a, done, valid)) {
                p->started = false;
            }
        } else {
            p->started = true;
            Tick latency = pkt->isWrite() ? PciEndpoint::write(pkt)
                                          : PciEndpoint::read(pkt);
            // 触发 FLR、复位或电源切换的配置写本身正常完成。
            p->epoch = pioEpoch;
            p->done = true;
            p->ready = curTick() + latency;
            wakePio(p->ready);
        }
    }
    while (!pioResponseBlocked && !pendingPio.empty()) {
        auto p = pendingPio.front();
        if (!p->done) {
            break;
        }
        if (p->ready > curTick()) {
            wakePio(p->ready);
            break;
        }
        if (p->linkDone) {
            auto done = std::move(p->linkDone);
            pendingPio.pop_front();
            done(p->packet);
            continue;
        }
        if (!sendTimingPioResponse(p->packet)) {
            pioResponseBlocked = true;
            break;
        }
        pendingPio.pop_front();
    }
    if (pioRequestRetry && pendingPio.size() < 16) {
        pioRequestRetry = false;
        retryTimingPioRequest();
    }
}

/** 按参数构造；全部组件由 Gw100PcieFactory 组装 */
Gw100PciDevice::Gw100PciDevice(const Params &p)
    : PciEndpoint(p),
      parts(Gw100PcieFactory::build(p, *this)),
      link(p.mgmt_link),
      wdtFatalPin(name() + ".wdt_fatal", 0, this),
      wdtFatalEvent([this] { parts->bootCtrl.setDevFatal(); },
                    name() + ".wdtFatal"),
      devResetReqPin(name() + ".dev_reset_req", 0, this),
      funcResetPin(name() + ".func_reset"),
      asyncMemory(p.async_memory),
      memoryIngresses(p.memory_ingresses),
      pioEvent([this] { processPio(); }, name() + ".asyncPio"),
      linkStats(this),
      linkEvent([this] { processLink(); }, name() + ".pcieLink", false,
                EventBase::CPU_Tick_Pri + 1)
{
    // TLP 携带的 Requester ID；gem5 的配置访问不带总线号，由参数给出
    parts->hostDma.requesterId = (uint32_t(p.pci_bus) << 8) |
                                 (uint32_t(devAddr().dev) << 3) |
                                 devAddr().func;
    parts->hif.pciState = [this] { return pciState(); };
    parts->bootCtrl.onDevReset = [this] { devResetRequest(); };
    parts->bootCtrl.onFlrDone = [this] {
        bool d0 = (parts->config.read(PmCsr, 2) & 3) == 0;
        parts->hdma.setHold(!d0);
        parts->intCtrl.setFlrGate(!d0);
        parts->hdi.setGate(!d0);
    };
    parts->intCtrl.onIntx = [this](bool level) {
        if (pcieTransport) {
            pcieTransport->setIntx(level);
            return;
        }
        if (level) {
            intrPost();
        } else {
            intrClear();
        }
    };
    funcResetPin.onChange([this](const bool &v) { funcReset(v); });
    parts->bar2Devmem.onReady = [this] { wakePio(curTick() + 1); };
    resetConfig = config();
    if (p.precise_link) {
        pcie::LinkConfig config;
        config.generation = p.link_speed;
        config.width = p.link_width;
        config.phyFeedbackPs = p.pcie_phy_feedback;
        config.phyGeneration = p.pcie_phy_generation;
        config.phyLaneMask = p.pcie_phy_lane_mask;
        config.equalizationPs = p.pcie_phy_equalization;
        if (!p.pcie_training_fail.empty()) {
            bool found = false;
            for (unsigned state = 1;
                 state <= unsigned(pcie::LinkState::HotReset); ++state) {
                if (p.pcie_training_fail ==
                    pcie::linkStateName(pcie::LinkState(state))) {
                    config.failTraining = pcie::LinkState(state);
                    found = true;
                    break;
                }
            }
            fatal_if(!found, "%s: invalid PCIe failure state", name());
        }
        pcie::LinkServices services;
        services.execute = [this](unsigned side, const pcie::Tlp &tlp,
                                  pcie::TargetDone done) {
            return executeLink(side, tlp, std::move(done));
        };
        services.intx = [this](bool asserted) {
            if (asserted) {
                intrPost();
            } else {
                intrClear();
            }
        };
        services.ready = [this] {
            if (parts->hostDma.onTxFree) {
                parts->hostDma.onTxFree();
            }
            wakeAt(curTick());
        };
        services.stateChanged = [this] {
            updateLinkStatus();
            if (pcieTransport &&
                pcieTransport->model().state() == pcie::LinkState::Down) {
                updateHdmaPcie();
            }
        };
        services.error = [this](unsigned side, pcie::LinkError error,
                                const std::vector<uint8_t> &header) {
            linkError(side, error, header);
        };
        services.observe = [this](const pcie::LinkTrace &trace) {
            traceLink(trace);
        };
        services.drained = [this] {
            if (linkDraining && !hostRequests && pendingPio.empty() &&
                drainState() == DrainState::Draining) {
                signalDrainDone();
            }
        };
        pcieTransport = std::make_unique<pcie::LinkTransport>(
            *this, config, *parts->hostDma.requesterId, p.pcie_cpl_timeout,
            p.pcie_stale_tag_quarantine, p.pcie_rcb, p.pcie_completion_payload,
            services);
        parts->hostDma.transport = pcieTransport.get();
        if (!p.pcie_fault.empty()) {
            const char *names[] = {"tlp_crc",  "dllp_crc",    "drop_tlp",
                                   "drop_ack", "drop_update", "framing",
                                   "duplicate"};
            auto fault =
                std::find(std::begin(names), std::end(names), p.pcie_fault);
            fatal_if(fault == std::end(names) || p.pcie_fault_side > 1,
                     "%s: invalid PCIe fault", name());
            pcieTransport->model().inject(
                p.pcie_fault_side, pcie::LinkFault(fault - std::begin(names)),
                p.pcie_fault_count);
        }
        if (!p.pcie_trace_path.empty()) {
            linkTrace.open(p.pcie_trace_path);
            fatal_if(!linkTrace, "%s: cannot open PCIe trace", name());
        }
    }
}

/** 按 PERST=2、Hot Reset=4、Link Down=8 重建配置和链路。 */
void
Gw100PciDevice::resetLink(Tick origin, unsigned cause)
{
    if (pcieTransport) {
        while (!hostIngress.empty()) {
            finishHost(hostIngress.front().packet, HostStatus::Ca, nullptr);
            hostIngress.pop_front();
        }
        if (cause == 4) {
            pcieTransport->model().hotReset();
        } else if (cause == 8) {
            pcieTransport->model().linkDown();
        } else {
            pcieTransport->model().setPerst(true);
        }
    }
    config() = resetConfig;
    for (unsigned i = 0; i < BARs.size(); ++i) {
        BARs[i]->write(upstreamInterface, config().baseAddr[i]);
    }
    parts->config.reset();
    parts->global.resetTimestamp(origin);
    lastBme = false;
    updateBusMaster();
    updateMsix();
    if (!pcieTransport || cause != 4) {
        updateHdmaPcie();
    }
    if (pcieTransport && cause != 4 && cause != 8) {
        pcieTransport->model().setPerst(false);
    }
    pioPort.sendRangeChange();
}

/** 检查端口，接入 ML 的 HIF 侧（接了管理链路时） */
void
Gw100PciDevice::init()
{
    // dma 端口由 HDMA 的 HostDmaPort 提供，DmaDevice 自带的 DmaPort
    // 不使用，所以跳过 DmaDevice::init() 对它的检查
    PioDevice::init();
    fatal_if(!parts->hostDma.isConnected(), "%s: dma 端口没有连接", name());
    parts->hostDma.toPhys = [this](Addr a) { return pciToDma(a); };
    if (!link)
        return;
    ml = &link->attach(Gw100MgmtLink::Side::Hif, *this, parts->hif);
    parts->hif.bindMl(ml);
    parts->bootCtrl.bindMl(ml);
    parts->hdmaMl.bind(ml);
    dbClient = ml->addClient();
    parts->hdi.sendDb = [this](unsigned slot, uint16_t value) {
        auto t = std::make_shared<MlTxn>();
        t->type = MlType::DbWr;
        t->addr = slot;
        t->len = 4;
        t->be = 0xF;
        t->data[0] = uint8_t(value);
        t->data[1] = uint8_t(value >> 8);
        return ml->submit(dbClient, t);
    };
    ml->onChange = [this] {
        // ML_HIF_ERR 非零经电平信号送到管理 Tile 侧（spec-mgmt-link
        // §8.2）；链路状态的变化交给启动与 FLR 控制和 HDMA
        ml->setHifErrLevel(ml->errReg() != 0);
        parts->bootCtrl.mlChanged();
        parts->hdma.wake();
        parts->hdi.trySendDoorbells();
    };
}

/** 撤销 ML HIF 侧的复位（接了管理链路时） */
void
Gw100PciDevice::startup()
{
    PciEndpoint::startup();
    if (pcieTransport &&
        pcieTransport->model().state() == pcie::LinkState::Down) {
        pcieTransport->model().setAspm(0, params().pcie_rc_aspm);
        pcieTransport->model().start();
    }
    // loadState 完成后安排事件，避免构造期的 tick 0 留入恢复事件队列。
    updateHdmaPcie();
    if (link)
        link->release(Gw100MgmtLink::Side::Hif);
}

/** PCI_STATE（spec-pcie §6.5）：配置空间中固件需要的状态 */
uint32_t
Gw100PciDevice::pciState()
{
    uint16_t cmd = letoh(config().command);
    uint32_t v = 0;
    if (cmd & PCI_CMD_MSE)
        v |= 1u << 0;
    if (cmd & PCI_CMD_BME)
        v |= 1u << 1;
    if (cmd & 0x0400)  // Interrupt Disable
        v |= 1u << 5;
    v |= (parts->config.read(PmCsr, 2) & 3) << 2;
    return v;
}

/**
 * 返回名为 if_name 的端口：dma 是 HDMA 访问 host 内存的端口，
 * mem_port 是设备内存端口，wdt_fatal 是看门狗第二级超时的信号，
 * dev_reset_req、func_reset 是设备完全复位的请求与复位信号，其余
 * 交给 PciEndpoint
 */
Port &
Gw100PciDevice::getPort(const std::string &if_name, PortID idx)
{
    if (if_name == "dma")
        return parts->hostDma;
    if (if_name == "mem_port")
        return parts->devmem.port();
    if (if_name == "bar2_mem_port") {
        return parts->bar2Devmem.port();
    }
    if (if_name == "wdt_fatal")
        return wdtFatalPin;
    if (if_name == "dev_reset_req") {
        fatal_if(idx > 0, "%s: dev_reset_req 只有一个", name());
        return devResetReqPin;
    }
    if (if_name == "func_reset")
        return funcResetPin;
    return PciEndpoint::getPort(if_name, idx);
}

/** wdt_fatal 信号置位：经两级同步器后置位 DEV_FATAL */
void
Gw100PciDevice::raiseInterruptPin(int num)
{
    (void)num;
    if (!wdtFatalEvent.scheduled())
        schedule(wdtFatalEvent, clockEdge(Cycles(2)));
}

/** wdt_fatal 信号撤销：它保持到复位，不会撤销 */
void
Gw100PciDevice::lowerInterruptPin(int num)
{
    (void)num;
}

/** host 写 DEV_RESET：进入 reset shim，请求 CRC 复位 */
void
Gw100PciDevice::devResetRequest()
{
    if (parts->shim.active())
        return;  // 复位已在进行
    // 入站完成器的读响应时间固定，接受 DEV_RESET 时没有交给功能逻辑
    // 而尚未完成的读，复位排空立即结束（spec-clock-reset-power §4.3）
    parts->shim.enter();
    if (devResetReqPin.isConnected()) {
        devResetReqPin.raise();
        return;
    }
    // 不接管理 Tile 的配置：没有 CRC，直接复位功能逻辑
    funcReset(true);
    funcReset(false);
}

/**
 * CRC 的复位信号变化：asserted 时复位功能逻辑和 ML 的 HIF 侧，
 * 撤销时释放 ML 并退出 reset shim
 */
void
Gw100PciDevice::funcReset(bool asserted)
{
    if (asserted) {
        invalidateMemory();
        DPRINTF(Gw100Pcie, "设备完全复位：复位功能逻辑\n");
        parts->devReset();
        parts->hostDma.cancelQueued();
        if (wdtFatalEvent.scheduled())
            deschedule(wdtFatalEvent);
        if (link)
            link->reset(Gw100MgmtLink::Side::Hif);
        if (devResetReqPin.isConnected())
            devResetReqPin.lower();
        return;
    }
    DPRINTF(Gw100Pcie, "设备完全复位：释放功能逻辑，退出 reset shim\n");
    if (link)
        link->release(Gw100MgmtLink::Side::Hif);
    parts->shim.exit();
}

/** 把一次 BAR 访问交给对应 BAR 的处理者，并完成响应 */
Tick
Gw100PciDevice::accessBar(PacketPtr pkt, bool is_write)
{
    int bar;
    Addr off;
    panic_if(!getBAR(pkt->getAddr(), bar, off),
             "%s: 访问地址 %#x 不在任何 BAR 内", name(), pkt->getAddr());
    BarHandler *h = parts->bars[bar];
    panic_if(!h, "%s: BAR%d 没有处理者", name(), bar);
    BarAccess a{off, unsigned(pkt->getSize()), is_write,
                const_cast<uint8_t *>(pkt->getConstPtr<uint8_t>())};
    if (pkt->isMaskedWrite()) {
        a.byteEnable = &pkt->req->getByteEnable();
    }
    const Tick lat =
        parts->shim.active() ? parts->shim.access(bar, a) : h->access(a);
    pkt->makeAtomicResponse();
    return lat;
}

/** BAR 读访问的入口：交给该 BAR 的处理者 */
Tick
Gw100PciDevice::readDevice(PacketPtr pkt)
{
    return accessBar(pkt, false);
}

/** BAR 写访问的入口：交给该 BAR 的处理者 */
Tick
Gw100PciDevice::writeDevice(PacketPtr pkt)
{
    return accessBar(pkt, true);
}

/** 配置读：0x40 以下交给基类，0x40 起交给 ConfigSpace */
Tick
Gw100PciDevice::readConfig(PacketPtr pkt)
{
    if (pcieTransport) {
        updateLinkStatus();
    }
    const Addr off = pkt->getAddr() & (CfgSpaceSize - 1);
    if (off < ConfigSpace::OwnBase)
        return PciDevice::readConfig(pkt);

    const unsigned size = pkt->getSize();
    panic_if(off + size > CfgSpaceSize, "%s: 配置空间访问越界", name());
    const uint32_t val = parts->config.read(off, size);
    switch (size) {
      case 1: pkt->setLE<uint8_t>(uint8_t(val)); break;
      case 2: pkt->setLE<uint16_t>(uint16_t(val)); break;
      case 4: pkt->setLE<uint32_t>(val); break;
      default: panic("%s: 非法的配置空间访问宽度 %u", name(), size);
    }
    pkt->makeAtomicResponse();
    return configDelay;
}

/**
 * 配置写：0x40 以下交给基类；0x40 起交给 ConfigSpace，并按返回的
 * 事件处理 FLR 与 D3hot→D0
 */
Tick
Gw100PciDevice::writeConfig(PacketPtr pkt)
{
    const Addr off = pkt->getAddr() & (CfgSpaceSize - 1);
    if (pkt->isMaskedWrite() && off < ConfigSpace::OwnBase) {
        // 原生 PCI 头部使用 DWORD 写入口，先按硬件 strobe 合并有效字节。
        uint8_t data[4];
        std::memcpy(data, reinterpret_cast<const uint8_t *>(&config()) + off,
                    sizeof(data));
        pkt->writeData(data);
        auto request = std::make_shared<Request>(pkt->getAddr(), 4, 0,
                                                 parts->devmem.id());
        Packet merged(request, MemCmd::WriteReq);
        merged.dataStatic(data);
        Tick latency = writeConfig(&merged);
        pkt->makeResponse();
        return latency;
    }
    if (off == PCI_COMMAND && pkt->getSize() == 2) {
        // gem5 的 PciDevice 对 16 位的 Command 写只取低 8 位，Interrupt
        // Disable（第 10 位）会丢失；按 32 位写转交（该路径只更新
        // Command，忽略 Status）
        cfgWriteCommon(PCI_COMMAND, 4, pkt->getLE<uint16_t>());
        pkt->makeAtomicResponse();
        updateBusMaster();
        return configDelay;
    }
    if (off < ConfigSpace::OwnBase) {
        Tick t = PciEndpoint::writeConfig(pkt);
        updateBusMaster();
        return t;
    }

    const unsigned size = pkt->getSize();
    panic_if(off + size > CfgSpaceSize, "%s: 配置空间访问越界", name());
    uint32_t val;
    switch (size) {
      case 1: val = pkt->getLE<uint8_t>(); break;
      case 2: val = pkt->getLE<uint16_t>(); break;
      case 4: val = pkt->getLE<uint32_t>(); break;
      default: panic("%s: 非法的配置空间访问宽度 %u", name(), size);
    }

    const CfgEvent ev =
        parts->config.writeMasked(off, size, val, pkt->req->getByteEnable());
    pkt->makeAtomicResponse();
    if (ev == CfgEvent::Flr) {
        functionReset("FLR");
    } else if (ev == CfgEvent::D0ToD3hot) {
        if (pcieTransport) {
            pcieTransport->setActive(false);
            pcieTransport->model().setD3hot(true);
        }
        invalidateMemory();
        parts->intCtrl.setFlrGate(true);
        parts->hdi.setGate(true);
        parts->hdma.reset();
        parts->hdma.setHold(true);
        parts->hdma.setBusMaster(false);
        parts->intCtrl.setBusMaster(false);
        parts->hostDma.cancelQueued();
        parts->bootCtrl.fwWriteBootStatus(parts->bootCtrl.fwBootStatus() &
                                          ~0x100u);
        parts->bootCtrl.hostEvent(1u << 4);
    } else if (ev == CfgEvent::D3hotToD0) {
        if (pcieTransport) {
            pcieTransport->model().setD3hot(false);
        }
        functionReset("D3hot→D0"); // No_Soft_Reset = 0
        parts->bootCtrl.hostEvent(1u << 4);
    }
    updateHdmaPcie();
    updateMsix();
    return configDelay;
}

/**
 * 构造一次配置写交给 PciEndpoint，用于复位配置空间头部（Command、
 * BAR），使 BAR 对象的状态一并更新
 */
void
Gw100PciDevice::cfgWriteCommon(Addr off, unsigned size, uint32_t val)
{
    Addr addr = upstreamInterface.configRange().start() + off;
    RequestPtr req =
        std::make_shared<Request>(addr, size, 0, parts->devmem.id());
    Packet pkt(req, MemCmd::WriteReq);
    uint8_t buf[4] = {};
    pkt.dataStatic(buf);
    if (size == 2)
        pkt.setLE<uint16_t>(uint16_t(val));
    else
        pkt.setLE<uint32_t>(val);
    PciEndpoint::writeConfig(&pkt);
}

/**
 * 功能级复位（FLR、D3hot→D0）：恢复配置空间和寄存器，清零
 * Command 和全部 BAR；设备内存内容保留
 */
void
Gw100PciDevice::functionReset(const char *why)
{
    invalidateMemory();
    DPRINTF(Gw100Pcie, "%s：复位配置空间、BAR 和寄存器\n", why);
    if (pcieTransport) {
        parts->config.resetFunction();
    } else {
        parts->config.reset();
    }
    parts->resetRegisters();
    cfgWriteCommon(PCI_COMMAND, 2, 0);
    for (int i = 0; i < 6; i++)
        cfgWriteCommon(PCI0_BASE_ADDR0 + 4 * i, 4, 0);
    // FLR 的门控、排空与通知固件（spec-pcie §13.2）；HDMA 已随寄存器
    // 复位，保持复位直到固件确认（spec-host-dma §8.4）
    parts->hdma.setHold(true);
    if (pcieTransport) {
        pcieTransport->setActive(false);
    }
    parts->intCtrl.setFlrGate(true);
    parts->hdi.setGate(true);
    parts->hostDma.cancelQueued();
    lastBme = false;
    parts->hdma.setBusMaster(false);
    parts->intCtrl.setBusMaster(false);
    parts->intCtrl.setIntxDisable(false);
    updateHdmaPcie();
    updateMsix();
    parts->bootCtrl.flr();
}

/** 按 Device Control 的 MPS、MRRS、Extended Tag 更新 HDMA */
void
Gw100PciDevice::updateHdmaPcie()
{
    uint32_t dc = parts->config.read(DevCtl, 2);
    HdmaEngine::PcieParams pp;
    pp.mps = 128u << ((dc >> 5) & 7);
    pp.mrrs = std::min(128u << ((dc >> 12) & 7),
                       unsigned(params().hdma_max_read_request));
    pp.extTag = (dc >> 8) & 1;
    pp.relaxed = dc & (1u << 4);
    pp.noSnoop = dc & (1u << 11);
    constexpr Tick timeouts[] = {0,
                                 pciedefs::CPL_TIMEOUT_A_SHORT_PS,
                                 pciedefs::CPL_TIMEOUT_A_LONG_PS,
                                 0,
                                 0,
                                 pciedefs::CPL_TIMEOUT_B_SHORT_PS,
                                 pciedefs::CPL_TIMEOUT_B_LONG_PS};
    unsigned timeout = parts->config.read(0x78, 2) & 15;
    pp.cplTimeout = timeout < std::size(timeouts) ? timeouts[timeout] : 0;
    if (pcieTransport) {
        bool configured = pcieTransport->configure(
            pp.mps, parts->config.read(0x60, 2) & 0x80);
        warn_if(!configured, "%s: incompatible live PCIe MPS change", name());
        pcieTransport->model().setAspm(1,
                                       (parts->config.read(0x60, 2) & 3) == 2);
    }
    parts->hdma.setPcie(pp);
}

/**
 * Command 寄存器变化：Bus Master Enable 通知 HDMA 与中断控制器，由 1
 * 变 0 时发出 BME_OFF 事件；Interrupt Disable 通知中断控制器
 */
void
Gw100PciDevice::updateBusMaster()
{
    uint16_t cmd = letoh(config().command);
    parts->intCtrl.setIntxDisable(cmd & CmdIntxDisable);
    bool bme = cmd & PCI_CMD_BME;
    if (bme == lastBme) {
        return;
    }
    bool enabled = bme && (parts->config.read(PmCsr, 2) & 3) == 0;
    if (pcieTransport) {
        pcieTransport->setActive(enabled);
    }
    parts->intCtrl.setBusMaster(enabled);
    lastBme = bme;
    parts->hdma.setBusMaster(enabled);
    if (!bme) {
        parts->bootCtrl.hostEvent(EvtBmeOff);
    }
}

/** MSI-X capability 的 Enable、Function Mask 变化：通知中断控制器 */
void
Gw100PciDevice::updateMsix()
{
    uint32_t mc = parts->config.read(MsixCtl, 2);
    parts->intCtrl.setMsix(mc & MsixCapability::Enable,
                           mc & MsixCapability::FuncMask);
}

/** 保存配置空间头部（基类）和各组件的状态 */
void
Gw100PciDevice::serialize(CheckpointOut &cp) const
{
    PciEndpoint::serialize(cp);
    parts->serialize(cp);
    fatal_if(pcieTransport, "数字链路 checkpoint 尚未接入");
}

/** 从 checkpoint 恢复 serialize() 保存的状态 */
void
Gw100PciDevice::unserialize(CheckpointIn &cp)
{
    PciEndpoint::unserialize(cp);
    parts->unserialize(cp);
    fatal_if(pcieTransport, "数字链路 checkpoint 尚未接入");
}

} // namespace gw100
} // namespace gem5
