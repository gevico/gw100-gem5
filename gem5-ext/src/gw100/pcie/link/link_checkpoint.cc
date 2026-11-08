/**
 * @file  link_checkpoint.cc
 * @brief 已排空 PCIe 链路的累计信用、序号和串行相位恢复
 *
 * @see spec-pcie
 */

#include "src/gw100/pcie/link/link_model.hh"

#include <stdexcept>

namespace gem5::gw100::pcie
{

/** 保存已排空的 L0/L1 状态，其他状态明确报错。 */
LinkSnapshot
LinkModel::snapshot() const
{
    if (!frozen || !empty() || !peer.quiescent() || !active(0) || !active(1) ||
        (linkState != LinkState::L0 && linkState != LinkState::L1)) {
        throw std::logic_error("PCIe checkpoint requires drained L0/L1");
    }
    LinkSnapshot result;
    result.tick = frozenAt;
    result.state = linkState;
    result.generation = actualGeneration;
    result.width = actualWidth;
    result.equalized = equalized;
    result.nextCookie = nextCookie;
    result.nextOrder = nextOrder;
    result.d3hot = d3hot;
    result.idleElapsed = frozenAt - lastTransaction;
    result.powerRetryRemaining =
        powerRetry > frozenAt ? powerRetry - frozenAt : 0;
    for (unsigned side = 0; side < 2; ++side) {
        const auto &port = ports[side];
        auto &saved = result.ports[side];
        saved.wire = port.wire.state();
        for (unsigned index = 0; index < 3; ++index) {
            saved.headers[index] = port.headers[index].state();
            saved.data[index] = port.data[index].state();
        }
        saved.allocatedHeaders = port.allocatedHeaders;
        saved.allocatedData = port.allocatedData;
        saved.nextTransmit = port.replay.nextSequence();
        saved.acknowledged = port.replay.acknowledgedSequence();
        saved.nextReceive = port.nextReceive;
        saved.flowRemaining = port.flowDue == Never ? port.flowRemaining
                              : port.flowDue > frozenAt
                                  ? port.flowDue - frozenAt
                                  : 0;
        saved.mps = port.mps;
        saved.extendedSynch = port.extendedSynch;
        saved.aspm = port.aspm;
        saved.roundRobin = port.roundRobin;
        saved.faults = port.faults;
        saved.stats = port.stats;
    }
    return result;
}

/** 恢复到排空状态，由 resume() 继续协议维护。 */
void
LinkModel::restore(const LinkSnapshot &saved)
{
    if (saved.version != 1 || saved.tick > clock.now() ||
        saved.generation < 1 || saved.generation > 4 ||
        saved.idleElapsed > saved.tick ||
        (saved.width != 1 && saved.width != 2 && saved.width != 4) ||
        (saved.state != LinkState::L0 && saved.state != LinkState::L1) ||
        (!frozen && (active(0) || active(1)))) {
        throw std::invalid_argument("PCIe checkpoint header");
    }
    for (const auto &port : saved.ports) {
        if (port.wire.generation != saved.generation ||
            port.wire.width != saved.width ||
            port.wire.dataStream != (saved.state == LinkState::L0) ||
            (port.mps != 128 && port.mps != 256) || port.roundRobin >= 3 ||
            port.flowRemaining == Never || port.nextTransmit > 4095 ||
            port.nextReceive > 4095 ||
            port.acknowledged != ((port.nextTransmit - 1) & 4095)) {
            throw std::invalid_argument("PCIe checkpoint port");
        }
        Serializer wire(1, 1);
        wire.restore(port.wire);
        for (unsigned index = 0; index < 3; ++index) {
            CreditCounter header(8);
            CreditCounter data(12);
            header.restore(port.headers[index]);
            data.restore(port.data[index]);
        }
    }
    resetPhysical(false);
    uint64_t delta = clock.now() - saved.tick;
    actualGeneration = saved.generation;
    actualWidth = saved.width;
    recoveryGeneration = actualGeneration;
    recoveryWidth = actualWidth;
    equalized = saved.equalized;
    nextCookie = saved.nextCookie;
    nextOrder = saved.nextOrder;
    d3hot = saved.d3hot;
    lastTransaction = clock.now() - saved.idleElapsed;
    powerRetry = clock.now() + saved.powerRetryRemaining;
    for (unsigned side = 0; side < 2; ++side) {
        auto &port = ports[side];
        const auto &source = saved.ports[side];
        auto wire = source.wire;
        wire.endHalf += delta * 2;
        wire.dataOriginHalf += delta * 2;
        wire.nextSkpHalf += delta * 2;
        port.wire.restore(wire);
        for (unsigned index = 0; index < 3; ++index) {
            port.headers[index].restore(source.headers[index]);
            port.data[index].restore(source.data[index]);
        }
        port.allocatedHeaders = source.allocatedHeaders;
        port.allocatedData = source.allocatedData;
        port.replay.restoreSequence(source.nextTransmit, source.acknowledged);
        port.nextReceive = source.nextReceive;
        port.phase = FcPhase::Active;
        port.flowRemaining = source.flowRemaining;
        port.flowDue = saved.state == LinkState::L0
                           ? clock.now() + source.flowRemaining
                           : Never;
        port.mps = source.mps;
        port.extendedSynch = source.extendedSynch;
        port.aspm = source.aspm;
        port.roundRobin = source.roundRobin;
        port.faults = source.faults;
        port.stats = source.stats;
        port.busyUntil = port.wire.endTick();
        port.idleSent = saved.state == LinkState::L1;
    }
    perst = false;
    stateDue = Never;
    frozen = true;
    draining = true;
    frozenAt = clock.now();
    setState(saved.state);
}

} // namespace gem5::gw100::pcie
