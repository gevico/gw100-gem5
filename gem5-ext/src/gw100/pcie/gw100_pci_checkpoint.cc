/**
 * @file  gw100_pci_checkpoint.cc
 * @brief 已排空 PCIe 链路、门控、相位和统计的命名字段保存与恢复
 *
 * @see spec-pcie
 */

#include "src/gw100/pcie/gw100_pci_device.hh"

namespace gem5::gw100
{

namespace
{

/** 对同一组命名字段编码和解码，格式版本由 LinkSnapshot 校验。 */
template <class Visitor>
void
visitLinkSnapshot(pcie::TransportSnapshot &transport, Visitor visit)
{
    visit("transport.endpointId", transport.endpointId);
    visit("transport.enabled", transport.enabled);
    visit("transport.intx", transport.intx);
    visit("transport.nextTag", transport.nextTag);
    auto &link = transport.link;
    visit("version", link.version);
    visit("tick", link.tick);
    visit("generation", link.generation);
    visit("width", link.width);
    visit("equalized", link.equalized);
    visit("nextCookie", link.nextCookie);
    visit("nextOrder", link.nextOrder);
    visit("d3hot", link.d3hot);
    visit("idleElapsed", link.idleElapsed);
    visit("powerRetryRemaining", link.powerRetryRemaining);
    unsigned state = unsigned(link.state);
    visit("state", state);
    link.state = pcie::LinkState(state);
    for (unsigned side = 0; side < 2; ++side) {
        auto &port = link.ports[side];
        std::string prefix = "port" + std::to_string(side) + ".";
        auto field = [&](const char *name, auto &value) {
            visit(prefix + name, value);
        };
        field("wire.generation", port.wire.generation);
        field("wire.width", port.wire.width);
        field("wire.bitHalf", port.wire.bitHalf);
        field("wire.endHalf", port.wire.endHalf);
        field("wire.dataOriginHalf", port.wire.dataOriginHalf);
        field("wire.dataSymbols", port.wire.dataSymbols);
        field("wire.nextSkpHalf", port.wire.nextSkpHalf);
        field("wire.dataStream", port.wire.dataStream);
        field("wire.active", port.wire.active);
        field("wire.controlSkpNext", port.wire.controlSkpNext);
        field("wire.trainingCount", port.wire.trainingCount);
        field("nextTransmit", port.nextTransmit);
        field("acknowledged", port.acknowledged);
        field("nextReceive", port.nextReceive);
        field("flowRemaining", port.flowRemaining);
        field("mps", port.mps);
        field("extendedSynch", port.extendedSynch);
        field("aspm", port.aspm);
        field("roundRobin", port.roundRobin);
        for (unsigned index = 0; index < 3; ++index) {
            visit(prefix + "allocatedHeaders" + std::to_string(index),
                  port.allocatedHeaders[index]);
            visit(prefix + "allocatedData" + std::to_string(index),
                  port.allocatedData[index]);
            for (unsigned kind = 0; kind < 2; ++kind) {
                auto &credit = kind ? port.data[index] : port.headers[index];
                std::string name = prefix + (kind ? "data" : "header") +
                                   std::to_string(index) + ".";
                visit(name + "fieldBits", credit.fieldBits);
                visit(name + "scale", credit.scale);
                visit(name + "consumed", credit.consumed);
                visit(name + "limit", credit.limit);
                visit(name + "initialized", credit.initialized);
                visit(name + "infinite", credit.infinite);
            }
        }
        for (unsigned fault = 0; fault < port.faults.size(); ++fault) {
            visit(prefix + "fault" + std::to_string(fault),
                  port.faults[fault]);
        }
        field("stats.tlpTransmit", port.stats.tlpTransmit);
        field("stats.tlpReceive", port.stats.tlpReceive);
        field("stats.payloadTransmit", port.stats.payloadTransmit);
        field("stats.payloadReceive", port.stats.payloadReceive);
        field("stats.dllpTransmit", port.stats.dllpTransmit);
        field("stats.dllpReceive", port.stats.dllpReceive);
        field("stats.replayTransmit", port.stats.replayTransmit);
        field("stats.duplicateReceive", port.stats.duplicateReceive);
        field("stats.crcErrors", port.stats.crcErrors);
        field("stats.creditStalls", port.stats.creditStalls);
        field("stats.replayStalls", port.stats.replayStalls);
        field("stats.queueWaitPs", port.stats.queueWaitPs);
        field("stats.packetHalf", port.stats.packetHalf);
        field("stats.maintenanceHalf", port.stats.maintenanceHalf);
        field("stats.trainingHalf", port.stats.trainingHalf);
    }
}

/** 对全部已注册统计使用相同的命名字段。 */
template <class Stats, class Visitor>
void
visitLinkStats(Stats &stats, Visitor visit)
{
    visit("accepted", stats.accepted);
    visit("transmitted", stats.transmitted);
    visit("payload", stats.payload);
    visit("replays", stats.replays);
    visit("dllps", stats.dllps);
    visit("errors", stats.errors);
    visit("packetTicks", stats.packetTicks);
    visit("maintenanceTicks", stats.maintenanceTicks);
    visit("trainingTicks", stats.trainingTicks);
    visit("queueTicks", stats.queueTicks);
    visit("requestTicks", stats.requestTicks);
    visit("completed", stats.completed);
    visit("targetTicks", stats.targetTicks);
}

} // anonymous namespace

/** 保存已排空的协议状态、事务层门控和统计。 */
void
Gw100PciDevice::serializeLink(CheckpointOut &cp) const
{
    if (!pcieTransport) {
        return;
    }
    fatal_if(hostRequests || !hostIngress.empty() || !hostResponses.empty() ||
                 !pendingPio.empty(),
             "%s: PCIe checkpoint requires drain", name());
    ScopedCheckpointSection section(cp, "pcie_link");
    auto saved = pcieTransport->snapshot();
    visitLinkSnapshot(saved, [&](const std::string &key, const auto &value) {
        paramOut(cp, key, value);
    });
    visitLinkStats(linkStats,
                   [&](const char *name, const statistics::Vector &stat) {
                       statistics::VCounter values;
                       stat.value(values);
                       arrayParamOut(cp, std::string("stats.") + name, values);
                   });
    paramOut(cp, "lastBme", lastBme);
}

/** 恢复已排空的协议状态、事务层门控和统计。 */
void
Gw100PciDevice::unserializeLink(CheckpointIn &cp)
{
    if (!pcieTransport) {
        return;
    }
    ScopedCheckpointSection section(cp, "pcie_link");
    pcie::TransportSnapshot saved;
    visitLinkSnapshot(saved, [&](const std::string &key, auto &value) {
        paramIn(cp, key, value);
    });
    pcieTransport->restore(saved);
    visitLinkStats(linkStats, [&](const char *name, statistics::Vector &stat) {
        statistics::VCounter values;
        arrayParamIn(cp, std::string("stats.") + name, values);
        fatal_if(values.size() != 2, "%s: invalid PCIe statistics",
                 this->name());
        for (unsigned side = 0; side < 2; ++side) {
            stat[side] = values[side];
        }
    });
    paramIn(cp, "lastBme", lastBme);
    linkDraining = true;
    linkDrainStarted = true;
}

} // namespace gem5::gw100
