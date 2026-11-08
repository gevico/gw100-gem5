/**
 * @file  gw100_pci_link.cc
 * @brief 共享数字链路与原生 Host PIO、DMA、中断和设备目标的连接
 *
 * Host 总线和内存继续提供自己的 timing 服务。完整 TLP 到达 RC 后才
 * 调用 Host 内存后端，EP 的寄存器与 BAR2 在收到线上请求后才执行。
 *
 * @see spec-pcie, spec-host-dma
 */

#include "src/gw100/pcie/gw100_pci_device.hh"

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <sstream>

#include "mem/request.hh"
#include "sim/byteswap.hh"

namespace gem5::gw100
{

/** 建立 RC、EP 两项向量。 */
Gw100PciDevice::LinkStats::LinkStats(statistics::Group *parent)
    : statistics::Group(parent, "pcie_link"),
      ADD_STAT(accepted, statistics::units::Count::get(), "本地接受的 TLP"),
      ADD_STAT(transmitted, statistics::units::Count::get(), "首次发送的 TLP"),
      ADD_STAT(payload, statistics::units::Byte::get(), "首次发送的有效载荷"),
      ADD_STAT(replays, statistics::units::Count::get(), "重放 TLP"),
      ADD_STAT(dllps, statistics::units::Count::get(), "发送 DLLP"),
      ADD_STAT(errors, statistics::units::Count::get(), "线上错误"),
      ADD_STAT(packetTicks, statistics::units::Tick::get(), "包串行占用"),
      ADD_STAT(maintenanceTicks, statistics::units::Tick::get(),
               "SKP 与 framing 占用"),
      ADD_STAT(trainingTicks, statistics::units::Tick::get(), "数字训练占用"),
      ADD_STAT(queueTicks, statistics::units::Tick::get(),
               "源端接受至 DLL 提交"),
      ADD_STAT(requestTicks, statistics::units::Tick::get(), "接受至全部完成"),
      ADD_STAT(completed, statistics::units::Count::get(),
               "已收到完整完成的请求"),
      ADD_STAT(targetTicks, statistics::units::Tick::get(),
               "原生目标累计服务时间")
{
    accepted.init(2);
    accepted.subname(0, "rc");
    accepted.subname(1, "ep");
    transmitted.init(2);
    transmitted.subname(0, "rc");
    transmitted.subname(1, "ep");
    payload.init(2);
    payload.subname(0, "rc");
    payload.subname(1, "ep");
    replays.init(2);
    replays.subname(0, "rc");
    replays.subname(1, "ep");
    dllps.init(2);
    dllps.subname(0, "rc");
    dllps.subname(1, "ep");
    errors.init(2);
    errors.subname(0, "rc");
    errors.subname(1, "ep");
    packetTicks.init(2);
    packetTicks.subname(0, "rc");
    packetTicks.subname(1, "ep");
    maintenanceTicks.init(2);
    maintenanceTicks.subname(0, "rc");
    maintenanceTicks.subname(1, "ep");
    trainingTicks.init(2);
    trainingTicks.subname(0, "rc");
    trainingTicks.subname(1, "ep");
    queueTicks.init(2);
    queueTicks.subname(0, "rc");
    queueTicks.subname(1, "ep");
    requestTicks.init(2);
    requestTicks.subname(0, "rc");
    requestTicks.subname(1, "ep");
    completed.init(2);
    completed.subname(0, "rc");
    completed.subname(1, "ep");
    targetTicks.init(2);
    targetTicks.subname(0, "rc");
    targetTicks.subname(1, "ep");
}

/** 在指定协议边界运行共享链路事件。 */
void
Gw100PciDevice::wakeAt(uint64_t when)
{
    when = std::max<uint64_t>(when, curTick());
    if (!linkEvent.scheduled()) {
        schedule(linkEvent, when);
    } else if (linkEvent.when() > when) {
        reschedule(linkEvent, when);
    }
}

/** Host PIO 最终完成，返回队列保留其全部反压占用。 */
void
Gw100PciDevice::finishHost(PacketPtr pkt, HostStatus status,
                           const uint8_t *data)
{
    if (pkt->isRead()) {
        if (status == HostStatus::Ok) {
            std::memcpy(pkt->getPtr<uint8_t>(), data, pkt->getSize());
        } else {
            std::memset(pkt->getPtr<uint8_t>(), 0xFF, pkt->getSize());
        }
    }
    pkt->makeResponse();
    if (status != HostStatus::Ok &&
        !upstreamInterface.configRange().contains(pkt->getAddr())) {
        pkt->setBadCommand();
    }
    hostResponses.push_back(pkt);
    wakeAt(curTick());
}

/** 处理共享链路、Host 准入与返回。 */
void
Gw100PciDevice::processLink()
{
    if (!pcieTransport) {
        return;
    }
    pcieTransport->tick();
    while (!hostIngress.empty()) {
        auto &entry = hostIngress.front();
        if (entry.ready > curTick()) {
            wakeAt(entry.ready);
            break;
        }
        PacketPtr packet = entry.packet;
        if (!pcieTransport->model().active(0)) {
            if (curTick() < entry.linkDeadline) {
                wakeAt(entry.linkDeadline);
                break;
            }
            pcieTransport->model().noteError(
                0, pcie::LinkError::TrainingTimeout, {});
            finishHost(packet, HostStatus::Ur, nullptr);
            hostIngress.pop_front();
            continue;
        }
        if (!pcieTransport->submitPio(
                entry.tlp,
                [this, packet](HostStatus status, const uint8_t *data) {
                    finishHost(packet, status, data);
                })) {
            break;
        }
        if (entry.tlp.traffic() == pcie::Traffic::Posted) {
            finishHost(packet, HostStatus::Ok, nullptr);
        }
        hostIngress.pop_front();
    }
    while (!pioResponseBlocked && !hostResponses.empty()) {
        if (!sendTimingPioResponse(hostResponses.front())) {
            pioResponseBlocked = true;
            break;
        }
        hostResponses.pop_front();
        --hostRequests;
    }
    if (pioRequestRetry && !linkDraining &&
        hostRequests < pciedefs::HOST_PACKET_SLOTS) {
        pioRequestRetry = false;
        retryTimingPioRequest();
    }
    if (linkDraining && hostIngress.empty() && !linkDrainStarted &&
        parts->hdma.quiescent() && parts->bootCtrl.quiescent()) {
        linkDrainStarted = true;
        pcieTransport->model().drain();
    }
    if (linkDraining && !hostRequests && pendingPio.empty() &&
        pcieTransport->model().isDrained() &&
        drainState() == DrainState::Draining) {
        signalDrainDone();
    }
}

/** 将线上请求交给 EP PIO 或 RC Host timing 目标。 */
bool
Gw100PciDevice::executeLink(unsigned side, const pcie::Tlp &tlp,
                            pcie::TargetDone done)
{
    using pcie::CompletionStatus;
    unsigned bytes = tlp.requestBytes();
    Addr address = tlp.address + tlp.firstOffset();
    if (!side) {
        Addr physical = pciToDma(address);
        auto contains = [physical, bytes](const AddrRange &range) {
            return physical + bytes > physical && range.contains(physical) &&
                   range.contains(physical + bytes - 1);
        };
        bool memory = std::any_of(params().pcie_host_ranges.begin(),
                                  params().pcie_host_ranges.end(), contains);
        bool interrupt =
            std::any_of(params().pcie_interrupt_ranges.begin(),
                        params().pcie_interrupt_ranges.end(), contains);
        bool write = tlp.kind == pcie::TlpKind::MemoryWrite;
        if (!memory && !(interrupt && write)) {
            done(CompletionStatus::Ur, {});
            return true;
        }
        if (interrupt &&
            !pcieTransport->precedingPostedVisible(side, tlp.cookie)) {
            return false;
        }
        traceService(side, tlp.cookie, false);
        auto complete = [this, side, cookie = tlp.cookie, bytes,
                         done](HostStatus status, const uint8_t *data) {
            traceService(side, cookie, true);
            std::vector<uint8_t> result;
            if (status == HostStatus::Ok && data) {
                result.assign(data, data + bytes);
            }
            done(status == HostStatus::Ok ? CompletionStatus::Success
                                          : CompletionStatus::Ur,
                 result);
        };
        auto execute = [this, tlp, address, bytes, complete] {
            std::vector<bool> mask(bytes);
            for (unsigned i = 0; i < bytes; ++i) {
                mask[i] = tlp.byteEnabled(tlp.firstOffset() + i);
            }
            if (tlp.kind == pcie::TlpKind::MemoryRead) {
                parts->hostDma.backendRead(address, bytes, complete);
            } else {
                parts->hostDma.backendWrite(
                    address, bytes, tlp.payload.data() + tlp.firstOffset(),
                    complete, mask);
            }
        };
        if (params().pcie_host_service) {
            schedule(
                new EventFunctionWrapper(execute, name() + ".rcService", true),
                curTick() + params().pcie_host_service);
        } else {
            execute();
        }
        return true;
    }
    if (pendingPio.size() >= pciedefs::BACKEND_SLOTS) {
        return false;
    }
    bool config_access = tlp.isConfig();
    bool write = tlp.kind == pcie::TlpKind::MemoryWrite ||
                 tlp.kind == pcie::TlpKind::ConfigWrite;
    if (config_access) {
        if (tlp.destination != *parts->hostDma.requesterId) {
            done(CompletionStatus::Ur, {});
            return true;
        }
        address = upstreamInterface.configRange().start() + tlp.address;
    } else {
        address = upstreamInterface.memAddr(address);
        int bar = -1;
        Addr offset = 0;
        if (!(letoh(config().command) & 2) || !getBAR(address, bar, offset) ||
            !parts->bars[bar]) {
            done(CompletionStatus::Ur, {});
            return true;
        }
    }
    unsigned native_bytes = config_access ? 4 : bytes;
    auto request = std::make_shared<Request>(address, native_bytes, 0,
                                             parts->devmem.id());
    std::vector<bool> byte_enable(native_bytes);
    for (unsigned i = 0; i < native_bytes; ++i) {
        byte_enable[i] =
            tlp.byteEnabled(i + (config_access ? 0 : tlp.firstOffset()));
    }
    PacketPtr packet =
        new Packet(request, write ? MemCmd::WriteReq : MemCmd::ReadReq);
    packet->allocate();
    if (write) {
        packet->setData(tlp.payload.data() +
                        (config_access ? 0 : tlp.firstOffset()));
    }
    request->setByteEnable(byte_enable);
    auto pending = std::make_shared<PendingPio>();
    pending->packet = packet;
    pending->epoch = pioEpoch;
    pending->ready = curTick();
    traceService(side, tlp.cookie, false);
    pending->linkDone = [this, side, cookie = tlp.cookie, done, write, bytes,
                         prefix = config_access ? tlp.firstOffset()
                                                : 0](PacketPtr response) {
        traceService(side, cookie, true);
        std::vector<uint8_t> data;
        if (!write && !response->isError()) {
            data.assign(response->getConstPtr<uint8_t>() + prefix,
                        response->getConstPtr<uint8_t>() + prefix + bytes);
        }
        done(response->isError() ? CompletionStatus::Ca
                                 : CompletionStatus::Success,
             data);
        delete response;
    };
    pendingPio.push_back(pending);
    wakePio(curTick());
    return true;
}

/** 实际数字链路状态更新配置空间。 */
void
Gw100PciDevice::updateLinkStatus()
{
    if (!pcieTransport) {
        return;
    }
    const auto &model = pcieTransport->model();
    uint16_t value = model.generation() | (model.width() << 4) | (1u << 12);
    // Endpoint 的 Link Training 与 DLL Active Reporting 位按规范保留为零。
    parts->config.setLive(0x62, 2, value);
    uint32_t status = parts->config.read(0x5A, 2) & ~(1u << 5);
    if (pcieTransport->pendingReads(1)) {
        status |= 1u << 5;
    }
    parts->config.setLive(0x5A, 2, status);
    parts->config.setLive(0x188, 4, model.active(1) ? 0x80000001 : 0);
    parts->config.setLive(0x1AC, 4,
                          model.active(1) && model.generation() == 4 ? 15 : 0);
}

/** 将一次线上错误归入 AER 与错误消息。 */
void
Gw100PciDevice::linkError(unsigned side, pcie::LinkError error,
                          const std::vector<uint8_t> &header)
{
    if (error == pcie::LinkError::Poisoned) {
        parts->status.raiseErr(StatusUnit::ErrRxPoisoned);
    }
    if (side == 1) {
        uint8_t code = parts->config.raiseAer(error, header);
        if (code && pcieTransport) {
            pcieTransport->sendErrorMessage(code);
        }
    }
}

/** 记录原始链路观测和所在系统时刻。 */
void
Gw100PciDevice::traceLink(const pcie::LinkTrace &trace)
{
    using pcie::LinkPoint;
    unsigned side = trace.side;
    auto &requests = observedRequests[side];
    auto entry = requests.find(trace.cookie);
    if (trace.point == LinkPoint::Accept) {
        pcie::Tlp tlp;
        std::string error;
        panic_if(!pcie::Tlp::decode(trace.bytes, tlp, error),
                 "%s: invalid observed TLP", name());
        requests.emplace(trace.cookie,
                         ObservedRequest{trace.tick, tlp.traffic()});
        ++linkStats.accepted[side];
    } else if (trace.point == LinkPoint::Commit) {
        panic_if(entry == requests.end(), "%s: missing TLP observer", name());
        linkStats.queueTicks[side] += trace.tick - entry->second.accepted;
    } else if (trace.point == LinkPoint::Transmit) {
        if (trace.replay) {
            ++linkStats.replays[side];
        } else {
            ++linkStats.transmitted[side];
            std::vector<uint8_t> body(trace.bytes.begin() + 2,
                                      trace.bytes.end() - 4);
            pcie::Tlp tlp;
            std::string error;
            if (pcie::Tlp::decode(body, tlp, error)) {
                linkStats.payload[side] += tlp.payload.size();
            }
        }
        linkStats.packetTicks[side] +=
            trace.frame.endTick() - trace.frame.beginTick();
    } else if (trace.point == LinkPoint::DllpTransmit) {
        ++linkStats.dllps[side];
        linkStats.packetTicks[side] +=
            trace.frame.endTick() - trace.frame.beginTick();
    } else if (trace.point == LinkPoint::Skip) {
        linkStats.maintenanceTicks[side] +=
            trace.frame.endTick() - trace.frame.beginTick();
    } else if (trace.point == LinkPoint::OrderedSet) {
        linkStats.trainingTicks[side] +=
            trace.frame.endTick() - trace.frame.beginTick();
    } else if (trace.point == LinkPoint::Error) {
        ++linkStats.errors[side];
    } else if (trace.point == LinkPoint::Complete) {
        if (entry != requests.end()) {
            linkStats.requestTicks[side] +=
                trace.tick - entry->second.accepted;
            ++linkStats.completed[side];
            requests.erase(entry);
        }
    } else if (trace.point == LinkPoint::Cancel ||
               trace.point == LinkPoint::Retired ||
               (trace.point == LinkPoint::Acknowledge &&
                entry != requests.end() &&
                entry->second.traffic != pcie::Traffic::NonPosted)) {
        requests.erase(trace.cookie);
    } else if (trace.point == LinkPoint::State &&
               trace.value == unsigned(pcie::LinkState::Down)) {
        observedRequests[0].clear();
        observedRequests[1].clear();
    }
    if (trace.point == LinkPoint::TargetBegin) {
        observedTargets[side].emplace(trace.cookie, trace.tick);
    } else if (trace.point == LinkPoint::TargetEnd) {
        auto target = observedTargets[side].find(trace.cookie);
        if (target != observedTargets[side].end()) {
            linkStats.targetTicks[side] += trace.tick - target->second;
            observedTargets[side].erase(target);
        }
    }
    if (!linkTrace.is_open()) {
        return;
    }
    auto hex = [](const std::vector<uint8_t> &bytes) {
        std::ostringstream output;
        output << std::hex << std::setfill('0');
        for (auto byte : bytes) {
            output << std::setw(2) << unsigned(byte);
        }
        return output.str();
    };
    linkTrace << "{\"point\":\"" << pcie::linkPointName(trace.point)
              << "\",\"tick\":" << trace.tick << ",\"side\":" << trace.side
              << ",\"cookie\":" << trace.cookie << ",\"value\":" << trace.value
              << ",\"replay\":" << trace.replay
              << ",\"generation\":" << pcieTransport->model().generation()
              << ",\"width\":" << pcieTransport->model().width() << ",\"mps\":"
              << (128u << ((parts->config.read(0x58, 2) >> 5) & 7))
              << ",\"begin_half\":" << trace.frame.beginHalf
              << ",\"end_half\":" << trace.frame.endHalf
              << ",\"encoding_half\":" << trace.frame.encodingHalf
              << ",\"padding_half\":" << trace.frame.paddingHalf
              << ",\"ordered_half\":" << trace.frame.orderedHalf
              << ",\"bytes\":\"" << hex(trace.bytes) << "\",\"framing\":\""
              << hex(trace.framing) << '"';
    for (unsigned endpoint = 0; endpoint < 2; ++endpoint) {
        auto resources = pcieTransport->model().resources(endpoint);
        linkTrace << ",\"resources" << endpoint << "\":[";
        bool first = true;
        auto units = [&](unsigned value) {
            if (!first) {
                linkTrace << ',';
            }
            first = false;
            linkTrace << value;
        };
        for (const auto &group : {resources.txHeaders, resources.txBytes,
                                  resources.rxHeaders, resources.rxBytes}) {
            for (auto value : group) {
                units(value);
            }
        }
        units(resources.replayBytes);
        units(resources.replayHeaders);
        linkTrace << ']';
    }
    linkTrace << "}\n";
}

/** 为原生目标开始或完成建立可分项统计的观测。 */
void
Gw100PciDevice::traceService(unsigned side, uint64_t cookie, bool complete)
{
    pcie::LinkTrace trace;
    trace.point =
        complete ? pcie::LinkPoint::TargetEnd : pcie::LinkPoint::TargetBegin;
    trace.tick = curTick();
    trace.side = side;
    trace.cookie = cookie;
    traceLink(trace);
}

/** 排空 Host、共享链路与目标完成。 */
DrainState
Gw100PciDevice::drain()
{
    if (!pcieTransport) {
        return PciEndpoint::drain();
    }
    linkDraining = true;
    wakeAt(curTick());
    return !hostRequests && pendingPio.empty() &&
                   pcieTransport->model().isDrained()
               ? DrainState::Drained
               : DrainState::Draining;
}

/** checkpoint 恢复后继续协议维护。 */
void
Gw100PciDevice::drainResume()
{
    PciEndpoint::drainResume();
    linkDraining = false;
    linkDrainStarted = false;
    if (pcieTransport) {
        pcieTransport->model().resume();
        wakeAt(curTick());
    }
}

} // namespace gem5::gw100

namespace gem5::gw100
{

/** 返回实际链路配置、有限资源与计数的 JSON 快照。 */
std::string
Gw100PciDevice::pcieReport()
{
    if (linkTrace.is_open()) {
        linkTrace.flush();
    }
    if (!pcieTransport) {
        return "{\"enabled\":false,\"profile\":\"compat-atomic-functional\"}";
    }
    const auto &model = pcieTransport->model();
    std::ostringstream output;
    output << "{\"enabled\":true,\"profile\":\"generic-pcie-link-v0.1\""
           << ",\"state\":\"" << pcie::linkStateName(model.state()) << "\""
           << ",\"generation\":" << model.generation()
           << ",\"width\":" << model.width()
           << ",\"rc_active\":" << model.active(0)
           << ",\"ep_active\":" << model.active(1)
           << ",\"controller_ps\":" << pciedefs::CONTROLLER_PERIOD_PS
           << ",\"propagation_ps\":" << pciedefs::ONE_WAY_PROPAGATION_PS
           << ",\"host_service_ps\":" << params().pcie_host_service
           << ",\"phy_feedback_ps\":" << params().pcie_phy_feedback
           << ",\"rcb\":" << params().pcie_rcb
           << ",\"completion_payload\":" << params().pcie_completion_payload
           << ",\"mps\":" << (128u << ((parts->config.read(0x58, 2) >> 5) & 7))
           << ",\"mrrs\":"
           << (128u << ((parts->config.read(0x58, 2) >> 12) & 7))
           << ",\"stats\":{";
    bool first = true;
    auto vector = [&output, &first](const char *name,
                                    const statistics::Vector &stat) {
        statistics::VCounter values;
        stat.value(values);
        if (!first) {
            output << ',';
        }
        first = false;
        output << '"' << name << "\":[" << values[0] << ',' << values[1]
               << ']';
    };
    vector("accepted", linkStats.accepted);
    vector("transmitted", linkStats.transmitted);
    vector("payload_bytes", linkStats.payload);
    vector("replays", linkStats.replays);
    vector("dllps", linkStats.dllps);
    vector("errors", linkStats.errors);
    vector("packet_ps", linkStats.packetTicks);
    vector("maintenance_ps", linkStats.maintenanceTicks);
    vector("training_ps", linkStats.trainingTicks);
    vector("queue_ps", linkStats.queueTicks);
    vector("request_ps", linkStats.requestTicks);
    vector("completed", linkStats.completed);
    vector("target_ps", linkStats.targetTicks);
    output << "}}";
    return output.str();
}

} // namespace gem5::gw100
