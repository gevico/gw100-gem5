/**
 * @file  link_training.cc
 * @brief PCIe 数字训练、速率切换和 L1 握手的实现
 *
 * 数字 PHY 的反馈只决定规定检查点是否成功。训练发送数量、串行占用、
 * 协议等待和失败期限均由状态机执行。
 *
 * @see spec-pcie
 */

#include "src/gw100/pcie/link/link_model.hh"

#include <algorithm>
#include <set>
#include <stdexcept>

namespace gem5::gw100::pcie
{

/** 从 Detect 开始建立链路。 */
void
LinkModel::start()
{
    if (linkState != LinkState::Down || perst) {
        throw std::logic_error("PCIe start outside reset");
    }
    resetPhysical(true);
}

/** 物理链路复位，旧代次传输和回调失效。 */
void
LinkModel::resetPhysical(bool restart)
{
    ++epoch;
    events.clear();
    std::array<std::map<uint64_t, bool>, 2> cancelled;
    for (unsigned side = 0; side < 2; ++side) {
        auto &port = ports[side];
        for (const auto &queue : port.transmit) {
            for (const auto &entry : queue) {
                cancelled[side][entry.tlp.cookie] = false;
            }
        }
        for (const auto &entry : port.replay.entries()) {
            cancelled[side][entry->cookie] = true;
        }
        port.replay.reset();
        for (auto &queue : port.transmit) {
            queue.clear();
        }
        port.transmitBytes = {};
        port.dllps.clear();
        port.replays.clear();
        port.receive.clear();
        for (auto &credit : port.headers) {
            credit.reset();
        }
        for (auto &credit : port.data) {
            credit.reset();
        }
        port.receiveHeaders = {};
        port.receiveBytes = {};
        port.allocatedHeaders = {pciedefs::RX_PH, pciedefs::RX_NPH, 0};
        port.allocatedData = {pciedefs::RX_PD_BYTES / 16,
                              pciedefs::RX_NPD_BYTES / 16, 0};
        port.phase = FcPhase::Feature;
        port.initialMask = 0;
        port.remoteFeature = false;
        port.remoteFeatureAck = false;
        port.busyUntil = clock.now();
        port.nextReceive = 0;
        port.nakScheduled = false;
        port.ackCount = 0;
        port.ackDue = Never;
        port.replayDue = Never;
        port.replayRemaining = Never;
        port.flowRemaining = Never;
        port.replayRounds = 0;
        port.flowDue = Never;
        port.roundRobin = 0;
        port.blockTlp = false;
        port.idleSent = false;
        port.wire.reset(1, config.width, clock.now());
    }
    actualGeneration = 1;
    actualWidth = config.width;
    recoveryGeneration = 1;
    recoveryWidth = actualWidth;
    stateDue = Never;
    trainingFailure = false;
    recoveryRequested = false;
    changingSpeed = false;
    changingWidth = false;
    equalized = 0;
    powerAccepted = false;
    powerRejected = false;
    powerAck = false;
    wakeRequested = false;
    frozen = false;
    setState(LinkState::Down);
    for (unsigned side = 0; side < 2; ++side) {
        for (const auto &[cookie, submitted] : cancelled[side]) {
            emit(LinkPoint::Cancel, side, cookie, submitted);
            peer.cancelled(side, cookie, submitted);
        }
    }
    if (restart && !perst) {
        setState(LinkState::DetectQuiet);
        stateDue = clock.now() + pciedefs::LT_DETECT_QUIET_PS;
    }
    if (!inTick) {
        arm();
    }
}

/** 发送足够的 TS 并在相应数字反馈检查点等待。 */
void
LinkModel::trainStep(LinkState state, unsigned minimum_tx,
                     unsigned receive_count, unsigned after_first,
                     uint64_t timeout_ps)
{
    setState(state);
    trainingFailure = config.failTraining == state;
    uint64_t symbol_ps = ports[0].wire.state().bitHalf * 5;
    uint64_t ts_ps = actualGeneration < 3
                         ? 16 * symbol_ps
                         : (130 * ports[0].wire.state().bitHalf + 1) / 2;
    uint64_t feedback = config.phyFeedbackPs + config.propagationPs;
    if (state >= LinkState::Equalization0 &&
        state <= LinkState::Equalization3) {
        feedback += config.equalizationPs;
        if (state == LinkState::Equalization2 ||
            state == LinkState::Equalization3) {
            feedback = std::max(feedback, pciedefs::LT_EQ_REQUEST_HOLD_MIN_PS);
        }
    }
    uint64_t receive_ps = ts_ps * receive_count + feedback;
    trainingFailure |= receive_ps >= timeout_ps;
    stateDue = clock.now();
    for (unsigned side = 0; side < 2; ++side) {
        auto &port = ports[side];
        uint64_t start = std::max(clock.now(), port.wire.endTick());
        if (trainingFailure) {
            auto frame = port.wire.training(0, start, timeout_ps);
            port.stats.trainingHalf += frame.endHalf - frame.beginHalf;
            emit(LinkPoint::OrderedSet, side, 0, unsigned(state), frame);
        } else if (after_first) {
            auto first = port.wire.training(1, start, ts_ps + feedback);
            port.stats.trainingHalf += first.endHalf - first.beginHalf;
            emit(LinkPoint::OrderedSet, side, 0, unsigned(state), first);
            port.wire.resetTrainingCount();
            auto rest =
                port.wire.training(after_first, first.endTick(),
                                   receive_ps > first.endTick() - start
                                       ? receive_ps - (first.endTick() - start)
                                       : 0);
            port.stats.trainingHalf += rest.endHalf - rest.beginHalf;
            emit(LinkPoint::OrderedSet, side, 0, unsigned(state), rest);
        } else {
            auto frame = port.wire.training(minimum_tx, start, receive_ps);
            port.stats.trainingHalf += frame.endHalf - frame.beginHalf;
            emit(LinkPoint::OrderedSet, side, 0, unsigned(state), frame);
        }
        port.busyUntil = port.wire.endTick();
        stateDue = std::max(stateDue, port.busyUntil);
    }
}

/** 进入 Configuration.Idle 或 Recovery.Idle 并发送 SDS 与 IDL。 */
void
LinkModel::enterIdle(bool recovery)
{
    setState(recovery ? LinkState::RecoveryIdle : LinkState::ConfigIdle);
    trainingFailure = config.failTraining == linkState;
    stateDue = clock.now();
    for (unsigned side = 0; side < 2; ++side) {
        auto &port = ports[side];
        auto stream = port.wire.beginStream(clock.now());
        port.stats.trainingHalf += stream.endHalf - stream.beginHalf;
        emit(LinkPoint::OrderedSet, side, 0, unsigned(linkState), stream);
        uint64_t wait = config.phyFeedbackPs + config.propagationPs;
        unsigned idle = pciedefs::LT_IDLE_RECEIVE_SYMBOLS +
                        pciedefs::LT_IDLE_TRANSMIT_SYMBOLS_AFTER_FIRST_RX;
        auto frame =
            port.wire.packet(idle * actualWidth, stream.endTick() + wait);
        port.stats.trainingHalf += frame.endHalf - stream.endHalf;
        emit(LinkPoint::OrderedSet, side, 0, unsigned(linkState), frame);
        port.busyUntil = frame.endTick();
        stateDue = std::max(stateDue, port.busyUntil);
    }
    if (trainingFailure) {
        stateDue = clock.now() + pciedefs::LT_CONFIGURATION_PS;
    }
}

/** L0 到达后启动或恢复 DLL。 */
void
LinkModel::enterL0()
{
    stateDue = Never;
    lastTransaction = clock.now();
    setState(LinkState::L0);
    for (unsigned side = 0; side < 2; ++side) {
        auto &port = ports[side];
        port.idleSent = false;
        port.blockTlp = false;
        if (port.phase != FcPhase::Active) {
            sendInitialization(side);
        } else {
            if (port.replay.size()) {
                uint64_t remaining = port.replayRemaining == Never
                                         ? replayLatency(port)
                                         : port.replayRemaining;
                port.replayDue = clock.now() + remaining;
            }
            uint64_t remaining = port.flowRemaining == Never
                                     ? pciedefs::FC_UPDATE_PS
                                     : port.flowRemaining;
            port.flowDue = clock.now() + remaining;
            port.replayRemaining = Never;
            port.flowRemaining = Never;
            peer.transmitReady(side);
        }
    }
    wakeRequested = false;
}

/** 开始保留数据链路状态的 Recovery。 */
void
LinkModel::beginRecovery()
{
    recoveryRequested = false;
    changingSpeed = recoveryGeneration != actualGeneration;
    changingWidth = recoveryWidth != actualWidth;
    for (unsigned side = 0; side < 2; ++side) {
        auto &port = ports[side];
        if (port.replayDue != Never) {
            port.replayRemaining = port.replayDue > clock.now()
                                       ? port.replayDue - clock.now()
                                       : 0;
        }
        if (port.flowDue != Never) {
            port.flowRemaining =
                port.flowDue > clock.now() ? port.flowDue - clock.now() : 0;
        }
        port.replayDue = Never;
        port.flowDue = Never;
        auto frame = port.wire.beginRecovery(clock.now());
        port.stats.trainingHalf += frame.endHalf - frame.beginHalf;
        emit(LinkPoint::OrderedSet, side, 0, unsigned(LinkState::RecoveryLock),
             frame);
    }
    trainStep(LinkState::RecoveryLock, 8, pciedefs::LT_RECOVERY_RECEIVE_TS, 0,
              pciedefs::LT_RECOVERY_LOCK_PS);
}

/** 执行下一训练阶段或其规定失败路径。 */
void
LinkModel::advanceTraining()
{
    stateDue = Never;
    if (trainingFailure) {
        trainingFailure = false;
        report(1, LinkError::TrainingTimeout);
        resetPhysical(true);
        return;
    }
    switch (linkState) {
        case LinkState::DetectQuiet:
            setState(LinkState::DetectActive);
            stateDue = clock.now() + config.phyFeedbackPs;
            break;
        case LinkState::DetectActive:
            detectionMask = config.phyLaneMask;
            if (config.failTraining == LinkState::DetectActive ||
                !detectionMask) {
                resetPhysical(true);
                break;
            }
            if (detectionMask != 15) {
                setState(LinkState::DetectRecheck);
                stateDue = clock.now() + pciedefs::LT_DETECT_QUIET_PS;
                break;
            }
            [[fallthrough]];
        case LinkState::DetectRecheck: {
            if (detectionMask != config.phyLaneMask) {
                resetPhysical(true);
                break;
            }
            unsigned lanes = 0;
            while (lanes < 4 && (detectionMask & (1u << lanes))) {
                ++lanes;
            }
            actualWidth = lanes >= 4 ? 4 : lanes >= 2 ? 2 : lanes ? 1 : 0;
            actualWidth = std::min(actualWidth, config.width);
            if (!actualWidth) {
                resetPhysical(true);
                break;
            }
            for (auto &port : ports) {
                port.wire.reset(1, actualWidth, clock.now());
            }
            trainStep(LinkState::PollingActive,
                      pciedefs::LT_POLLING_ACTIVE_MIN_TS1,
                      pciedefs::LT_POLLING_RECEIVE_CONSECUTIVE, 0,
                      pciedefs::LT_POLLING_ACTIVE_PS);
            break;
        }
        case LinkState::PollingActive:
            trainStep(LinkState::PollingConfiguration, 0,
                      pciedefs::LT_POLLING_RECEIVE_CONSECUTIVE,
                      pciedefs::LT_POLLING_CONFIG_MIN_TS2_AFTER_FIRST_RX,
                      pciedefs::LT_POLLING_CONFIGURATION_PS);
            break;
        case LinkState::PollingConfiguration:
            trainStep(LinkState::ConfigWidthStart, 2,
                      pciedefs::LT_CONFIG_LANE_RECEIVE_TS1, 0,
                      pciedefs::LT_CONFIGURATION_PS);
            break;
        case LinkState::ConfigWidthStart:
            trainStep(LinkState::ConfigWidthAccept, 2,
                      pciedefs::LT_CONFIG_LANE_RECEIVE_TS1, 0,
                      pciedefs::LT_CONFIGURATION_PS);
            break;
        case LinkState::ConfigWidthAccept:
            trainStep(LinkState::ConfigLaneWait, 2,
                      pciedefs::LT_CONFIG_LANE_RECEIVE_TS1, 0,
                      pciedefs::LT_CONFIGURATION_PS);
            break;
        case LinkState::ConfigLaneWait:
            trainStep(LinkState::ConfigLaneAccept, 2,
                      pciedefs::LT_CONFIG_LANE_RECEIVE_TS1, 0,
                      pciedefs::LT_CONFIGURATION_PS);
            break;
        case LinkState::ConfigLaneAccept:
            trainStep(LinkState::ConfigComplete, 0,
                      pciedefs::LT_CONFIG_COMPLETE_RECEIVE_TS2,
                      pciedefs::LT_CONFIG_COMPLETE_MIN_TS2_AFTER_FIRST_RX,
                      pciedefs::LT_CONFIGURATION_PS);
            break;
        case LinkState::ConfigComplete:
            changingWidth = false;
            enterIdle(ports[0].phase == FcPhase::Active);
            break;
        case LinkState::ConfigIdle:
        case LinkState::RecoveryIdle:
            enterL0();
            break;
        case LinkState::RecoveryLock:
            if (!changingSpeed && actualGeneration >= 3 &&
                !(equalized & (1u << actualGeneration))) {
                trainStep(LinkState::Equalization0, 2,
                          pciedefs::LT_EQ_PHASE_RECEIVE_TS1, 0,
                          pciedefs::LT_EQUALIZATION_EP_PHASE01_PS);
            } else {
                unsigned after =
                    changingSpeed
                        ? (actualGeneration < 3
                               ? pciedefs::LT_RECOVERY_SPEED_TS2_8B10B
                               : pciedefs::LT_RECOVERY_SPEED_TS2_128B130B)
                        : 0;
                trainStep(LinkState::RecoveryConfiguration, 8,
                          pciedefs::LT_RECOVERY_RECEIVE_TS, after,
                          pciedefs::LT_RECOVERY_CONFIG_PS);
            }
            break;
        case LinkState::RecoveryConfiguration:
            if (changingSpeed) {
                setState(LinkState::RecoverySpeed);
                for (unsigned side = 0; side < 2; ++side) {
                    auto frame = ports[side].wire.electricalIdle(clock.now());
                    ports[side].stats.trainingHalf +=
                        frame.endHalf - frame.beginHalf;
                    emit(LinkPoint::OrderedSet, side, 0,
                         unsigned(LinkState::RecoverySpeed), frame);
                    stateDue = stateDue == Never
                                   ? frame.endTick()
                                   : std::max(stateDue, frame.endTick());
                }
                stateDue +=
                    config.propagationPs + pciedefs::LT_SPEED_IDLE_SUCCESS_PS;
            } else if (changingWidth) {
                actualWidth = recoveryWidth;
                for (auto &port : ports) {
                    port.wire.reset(actualGeneration, actualWidth,
                                    clock.now());
                }
                trainStep(LinkState::ConfigWidthStart, 2,
                          pciedefs::LT_CONFIG_LANE_RECEIVE_TS1, 0,
                          pciedefs::LT_CONFIGURATION_PS);
            } else {
                enterIdle(true);
            }
            break;
        case LinkState::RecoverySpeed:
            actualGeneration = recoveryGeneration;
            changingSpeed = false;
            for (unsigned side = 0; side < 2; ++side) {
                auto &port = ports[side];
                port.wire.reset(actualGeneration, actualWidth, clock.now());
                auto frame = port.wire.beginRecovery(clock.now());
                port.stats.trainingHalf += frame.endHalf - frame.beginHalf;
                emit(LinkPoint::OrderedSet, side, 0,
                     unsigned(LinkState::RecoveryLock), frame);
            }
            trainStep(LinkState::RecoveryLock, 8,
                      pciedefs::LT_RECOVERY_RECEIVE_TS, 0,
                      pciedefs::LT_RECOVERY_LOCK_PS);
            break;
        case LinkState::Equalization0:
            trainStep(LinkState::Equalization1, 2,
                      pciedefs::LT_EQ_PHASE_RECEIVE_TS1, 0,
                      pciedefs::LT_EQUALIZATION_EP_PHASE01_PS);
            break;
        case LinkState::Equalization1:
            trainStep(LinkState::Equalization2, 2,
                      pciedefs::LT_EQ_PHASE_RECEIVE_TS1, 0,
                      pciedefs::LT_EQUALIZATION_OTHER_PHASE_PS);
            break;
        case LinkState::Equalization2:
            trainStep(LinkState::Equalization3, 2,
                      pciedefs::LT_EQ_PHASE_RECEIVE_TS1, 0,
                      pciedefs::LT_EQUALIZATION_OTHER_PHASE_PS);
            break;
        case LinkState::Equalization3:
            equalized |= 1u << actualGeneration;
            trainStep(LinkState::RecoveryLock, 8,
                      pciedefs::LT_RECOVERY_RECEIVE_TS, 0,
                      pciedefs::LT_RECOVERY_LOCK_PS);
            break;
        case LinkState::L1Entry:
            setState(LinkState::L1);
            if (wakeRequested) {
                wakeLink();
            }
            break;
        case LinkState::L1:
            if (wakeRequested) {
                wakeLink();
            }
            break;
        case LinkState::HotReset:
            resetPhysical(true);
            break;
        default:
            break;
    }
}

/** 请求保留序号和信用的 Recovery，可改变目标速率或宽度。 */
void
LinkModel::retrain(unsigned generation, unsigned width)
{
    if (generation < 1 || generation > 4 ||
        (width != 1 && width != 2 && width != 4)) {
        throw std::invalid_argument("PCIe retrain target");
    }
    recoveryGeneration = std::min(generation, config.phyGeneration);
    unsigned lanes = 0;
    while (lanes < 4 && (config.phyLaneMask & (1u << lanes))) {
        ++lanes;
    }
    recoveryWidth = std::min(width, lanes >= 4 ? 4u : lanes >= 2 ? 2u : 1u);
    recoveryRequested = true;
    if (linkState == LinkState::L1) {
        wakeLink();
    }
    if (!inTick) {
        clock.wakeAt(clock.now());
    }
}

/** 立即门控新提交，PERST 撤销后重新训练。 */
void
LinkModel::setPerst(bool asserted)
{
    if (perst == asserted) {
        return;
    }
    perst = asserted;
    resetPhysical(!asserted);
}

/** 物理链路丢失，作废链路代次并重新执行 Detect。 */
void
LinkModel::linkDown()
{
    report(0, LinkError::SurpriseDown);
    resetPhysical(true);
}

/** 在正常链路上发起 Hot Reset 握手。 */
void
LinkModel::hotReset()
{
    if (linkState != LinkState::L0) {
        return;
    }
    setState(LinkState::HotReset);
    stateDue = clock.now();
    for (unsigned side = 0; side < 2; ++side) {
        auto &port = ports[side];
        port.wire.beginRecovery(clock.now());
        auto frame = port.wire.training(2, clock.now(), config.phyFeedbackPs);
        port.stats.trainingHalf += frame.endHalf - frame.beginHalf;
        emit(LinkPoint::OrderedSet, side, 0, unsigned(LinkState::HotReset),
             frame);
        stateDue = std::max(stateDue, frame.endTick());
    }
    stateDue += config.propagationPs + pciedefs::LT_HOT_RESET_RECEIVER_HOLD_PS;
    arm();
}

/** 软件为指定侧使能或关闭 ASPM L1。 */
void
LinkModel::setAspm(unsigned side, bool enabled)
{
    ports.at(side).aspm = enabled;
    if (!enabled && linkState == LinkState::L1) {
        wakeLink();
    }
    if (!inTick) {
        clock.wakeAt(clock.now());
    }
}

/** EP 的 D3hot 状态触发 PCI-PM L1，D0 请求恢复。 */
void
LinkModel::setD3hot(bool enabled)
{
    d3hot = enabled;
    if (!enabled) {
        wakeLink();
    }
    if (!inTick) {
        clock.wakeAt(clock.now());
    }
}

/** 当前是否可进入 L1 协商。 */
bool
LinkModel::canEnterL1() const
{
    if (!active(0) || !active(1) || !peer.quiescent() ||
        (!d3hot && (!ports[0].aspm || !ports[1].aspm))) {
        return false;
    }
    for (const auto &port : ports) {
        if (!port.receive.empty()) {
            return false;
        }
        for (const auto &queue : port.transmit) {
            if (!queue.empty()) {
                return false;
            }
        }
    }
    return true;
}

/** 启动 EP 发起的 ASPM 或 PCI-PM L1 协商。 */
void
LinkModel::beginL1()
{
    pciPower = d3hot;
    powerAccepted = false;
    powerRejected = false;
    powerAck = false;
    wakeRequested = false;
    ports[1].blockTlp = true;
    ports[0].idleSent = false;
    ports[1].idleSent = false;
    setState(LinkState::L1Negotiation);
}

/** 推进 PM DLLP、EIOS 和 L1 最短停留要求。 */
void
LinkModel::progressPower()
{
    if (linkState == LinkState::L0) {
        unsigned target = std::min(config.generation, config.phyGeneration);
        if (active(0) && active(1) && actualGeneration < target &&
            !recoveryRequested) {
            retrain(actualGeneration < 3 && target >= 3 ? 3 : target,
                    actualWidth);
        }
        if (!draining && !recoveryRequested && canEnterL1() &&
            clock.now() >= powerRetry &&
            (d3hot ||
             clock.now() >= lastTransaction + pciedefs::ASPM_IDLE_PS)) {
            beginL1();
        }
    }
    if (linkState != LinkState::L1Negotiation) {
        return;
    }
    if (!powerAck) {
        if (!ports[1].replay.size() && !ports[1].idleSent) {
            Dllp request;
            request.type = pciPower ? 0x20 : 0x23;
            queueDllp(1, request);
        }
        if (powerAccepted && !ports[0].replay.size()) {
            Dllp ack;
            ack.type = 0x24;
            queueDllp(0, ack);
        }
        return;
    }
    // 各方向最后一个 PM DLLP 结束后才能发送 EIOS。
    for (unsigned side : {1u, 0u}) {
        auto &port = ports[side];
        if (port.idleSent || port.busyUntil > clock.now()) {
            continue;
        }
        if (!side &&
            (!ports[1].idleSent ||
             clock.now() < ports[1].busyUntil + config.propagationPs)) {
            continue;
        }
        port.dllps.clear();
        auto frame = port.wire.electricalIdle(clock.now());
        port.busyUntil = frame.endTick();
        port.idleSent = true;
        port.flowRemaining = port.flowDue == Never ? Never
                             : port.flowDue > clock.now()
                                 ? port.flowDue - clock.now()
                                 : 0;
        port.flowDue = Never;
        port.stats.trainingHalf += frame.endHalf - frame.beginHalf;
        emit(LinkPoint::OrderedSet, side, 0, unsigned(LinkState::L1Entry),
             frame);
        if (side) {
            at(frame.endTick() + config.propagationPs, [] {});
        }
    }
    if (ports[0].idleSent && ports[1].idleSent) {
        stateDue = std::max(ports[0].busyUntil, ports[1].busyUntil) +
                   pciedefs::L1_IDLE_MIN_PS;
        setState(LinkState::L1Entry);
    }
}

/** 请求恢复 L1，协商中则先完成进入。 */
void
LinkModel::wakeLink()
{
    if (linkState != LinkState::L1 && linkState != LinkState::L1Negotiation &&
        linkState != LinkState::L1Entry) {
        return;
    }
    wakeRequested = true;
    if (linkState != LinkState::L1) {
        return;
    }
    uint64_t guard = std::max(ports[0].busyUntil, ports[1].busyUntil) +
                     (actualGeneration > 1 ? pciedefs::L1_EXIT_GUARD_PS
                                           : pciedefs::L1_IDLE_MIN_PS);
    if (clock.now() < guard) {
        stateDue = guard;
    } else {
        recoveryGeneration = actualGeneration;
        recoveryWidth = actualWidth;
        beginRecovery();
    }
    if (!inTick) {
        arm();
    }
}

/** 稳定的状态名，用于 trace 和报告。 */
const char *
linkStateName(LinkState state)
{
    constexpr const char *names[] = {"Down",
                                     "Detect.Quiet",
                                     "Detect.Active",
                                     "Detect.Recheck",
                                     "Polling.Active",
                                     "Polling.Configuration",
                                     "Configuration.Linkwidth.Start",
                                     "Configuration.Linkwidth.Accept",
                                     "Configuration.Lanenum.Wait",
                                     "Configuration.Lanenum.Accept",
                                     "Configuration.Complete",
                                     "Configuration.Idle",
                                     "L0",
                                     "Recovery.RcvrLock",
                                     "Recovery.RcvrCfg",
                                     "Recovery.Speed",
                                     "Recovery.Equalization.Phase0",
                                     "Recovery.Equalization.Phase1",
                                     "Recovery.Equalization.Phase2",
                                     "Recovery.Equalization.Phase3",
                                     "Recovery.Idle",
                                     "L1.Negotiation",
                                     "L1.Entry",
                                     "L1",
                                     "HotReset",
                                     "Disabled"};
    return unsigned(state) < std::size(names) ? names[unsigned(state)]
                                              : "Invalid";
}

/** 稳定的观测点名，用于独立检查器。 */
const char *
linkPointName(LinkPoint point)
{
    constexpr const char *names[] = {"ACCEPT",
                                     "DLL_COMMIT",
                                     "TLP_TX",
                                     "RX_COMPLETE",
                                     "ACK_RETURN",
                                     "TARGET_VISIBLE",
                                     "REQUEST_COMPLETE",
                                     "DLLP_TX",
                                     "DLLP_RX",
                                     "SKP",
                                     "ORDERED_SET",
                                     "STATE",
                                     "ERROR",
                                     "CANCEL",
                                     "DRAINED",
                                     "TARGET_BEGIN",
                                     "TARGET_END",
                                     "REQUEST_RETIRED"};
    return unsigned(point) < std::size(names) ? names[unsigned(point)]
                                              : "Invalid";
}

} // namespace gem5::gw100::pcie
