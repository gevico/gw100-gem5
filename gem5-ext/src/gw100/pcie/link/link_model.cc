/**
 * @file  link_model.cc
 * @brief PCIe 双向事务、数据链路和资源调度的实现
 *
 * @see spec-pcie
 */

#include "src/gw100/pcie/link/link_model.hh"

#include <algorithm>
#include <stdexcept>

namespace gem5::gw100::pcie
{

/** 按共享档案创建有限资源。 */
LinkModel::Port::Port()
    : replay(pciedefs::REPLAY_DATA_BYTES, pciedefs::REPLAY_DESCRIPTOR_SLOTS,
             pciedefs::REPLAY_ALIGNMENT_BYTES)
{
    allocatedHeaders = {pciedefs::RX_PH, pciedefs::RX_NPH, 0};
    allocatedData = {pciedefs::RX_PD_BYTES / 16, pciedefs::RX_NPD_BYTES / 16,
                     0};
}

/** 构造后由 start() 执行真实数字训练。 */
LinkModel::LinkModel(LinkClockIf &clock, LinkPeerIf &peer,
                     const LinkConfig &config)
    : clock(clock), peer(peer), config(config)
{
    if (config.generation < 1 || config.generation > 4 ||
        config.phyGeneration < 1 || config.phyGeneration > 4 ||
        (config.width != 1 && config.width != 2 && config.width != 4) ||
        config.phyLaneMask > 15 || !config.controllerPs) {
        throw std::invalid_argument("PCIe link configuration");
    }
    constexpr LinkState failure_states[] = {LinkState::Down,
                                            LinkState::DetectActive,
                                            LinkState::PollingActive,
                                            LinkState::PollingConfiguration,
                                            LinkState::ConfigWidthStart,
                                            LinkState::ConfigWidthAccept,
                                            LinkState::ConfigLaneWait,
                                            LinkState::ConfigLaneAccept,
                                            LinkState::ConfigComplete,
                                            LinkState::ConfigIdle,
                                            LinkState::RecoveryLock,
                                            LinkState::RecoveryConfiguration,
                                            LinkState::RecoveryIdle,
                                            LinkState::Equalization0,
                                            LinkState::Equalization1,
                                            LinkState::Equalization2,
                                            LinkState::Equalization3};
    if (std::find(std::begin(failure_states), std::end(failure_states),
                  config.failTraining) == std::end(failure_states)) {
        throw std::invalid_argument("PCIe unsupported PHY failure phase");
    }
}

/** 下一控制器沿加适配流水，单位 ps。 */
uint64_t
LinkModel::adapterReady(uint64_t when) const
{
    return ((when + config.controllerPs - 1) / config.controllerPs +
            pciedefs::ADAPTER_CYCLES) *
           config.controllerPs;
}

/** 某类待发或接收头部实体容量。 */
unsigned
LinkModel::headerCapacity(Traffic traffic, bool receive) const
{
    constexpr unsigned tx[] = {pciedefs::TX_PH, pciedefs::TX_NPH,
                               pciedefs::TX_CPLH};
    constexpr unsigned rx[] = {pciedefs::RX_PH, pciedefs::RX_NPH,
                               pciedefs::COMPLETION_HEADER_SLOTS};
    return receive ? rx[unsigned(traffic)] : tx[unsigned(traffic)];
}

/** 某类待发或接收数据实体容量，单位 B。 */
unsigned
LinkModel::dataCapacity(Traffic traffic, bool receive) const
{
    constexpr unsigned tx[] = {pciedefs::TX_PD_BYTES, pciedefs::TX_NPD_BYTES,
                               pciedefs::TX_CPLD_BYTES};
    constexpr unsigned rx[] = {pciedefs::RX_PD_BYTES, pciedefs::RX_NPD_BYTES,
                               pciedefs::COMPLETION_DATA_BYTES};
    return receive ? rx[unsigned(traffic)] : tx[unsigned(traffic)];
}

/** 本地实体缓冲对载荷的分配量，单位 B。 */
unsigned
LinkModel::dataAllocation(const Tlp &tlp) const
{
    // 完成使用请求者预留的 DWORD 数据缓冲，PH/PD 则按实体 FC unit 分配。
    if (tlp.traffic() == Traffic::Completion) {
        return tlp.payload.size();
    }
    return (tlp.payload.size() + 15) / 16 * 16;
}

/** 所选 ACK 最大延迟，单位 ps。 */
uint64_t
LinkModel::ackLatency(const Port &port) const
{
    constexpr unsigned limits[4][2][3] = {
        {{pciedefs::GEN1_MPS128_X1_ACK_SYMBOLS,
          pciedefs::GEN1_MPS128_X2_ACK_SYMBOLS,
          pciedefs::GEN1_MPS128_X4_ACK_SYMBOLS},
         {pciedefs::GEN1_MPS256_X1_ACK_SYMBOLS,
          pciedefs::GEN1_MPS256_X2_ACK_SYMBOLS,
          pciedefs::GEN1_MPS256_X4_ACK_SYMBOLS}},
        {{pciedefs::GEN2_MPS128_X1_ACK_SYMBOLS,
          pciedefs::GEN2_MPS128_X2_ACK_SYMBOLS,
          pciedefs::GEN2_MPS128_X4_ACK_SYMBOLS},
         {pciedefs::GEN2_MPS256_X1_ACK_SYMBOLS,
          pciedefs::GEN2_MPS256_X2_ACK_SYMBOLS,
          pciedefs::GEN2_MPS256_X4_ACK_SYMBOLS}},
        {{pciedefs::GEN3_MPS128_X1_ACK_SYMBOLS,
          pciedefs::GEN3_MPS128_X2_ACK_SYMBOLS,
          pciedefs::GEN3_MPS128_X4_ACK_SYMBOLS},
         {pciedefs::GEN3_MPS256_X1_ACK_SYMBOLS,
          pciedefs::GEN3_MPS256_X2_ACK_SYMBOLS,
          pciedefs::GEN3_MPS256_X4_ACK_SYMBOLS}},
        {{pciedefs::GEN4_MPS128_X1_ACK_SYMBOLS,
          pciedefs::GEN4_MPS128_X2_ACK_SYMBOLS,
          pciedefs::GEN4_MPS128_X4_ACK_SYMBOLS},
         {pciedefs::GEN4_MPS256_X1_ACK_SYMBOLS,
          pciedefs::GEN4_MPS256_X2_ACK_SYMBOLS,
          pciedefs::GEN4_MPS256_X4_ACK_SYMBOLS}},
    };
    constexpr uint64_t symbols[] = {
        pciedefs::GEN1_SYMBOL_PS, pciedefs::GEN2_SYMBOL_PS,
        pciedefs::GEN3_SYMBOL_PS, pciedefs::GEN4_SYMBOL_PS};
    unsigned width_index = actualWidth == 1 ? 0 : actualWidth == 2 ? 1 : 2;
    return limits[actualGeneration - 1][port.mps == 256][width_index] *
           symbols[actualGeneration - 1];
}

/** 所选 replay 期限，单位 ps。 */
uint64_t
LinkModel::replayLatency(const Port &port) const
{
    constexpr uint64_t symbols[] = {
        pciedefs::GEN1_SYMBOL_PS, pciedefs::GEN2_SYMBOL_PS,
        pciedefs::GEN3_SYMBOL_PS, pciedefs::GEN4_SYMBOL_PS};
    uint64_t limit = port.extendedSynch ? pciedefs::REPLAY_EXTENDED_SYMBOLS
                                        : pciedefs::REPLAY_SYMBOLS;
    return limit * symbols[actualGeneration - 1];
}

/** 在物理链路代次内安排一次传输或 RX 释放事件。 */
void
LinkModel::at(uint64_t when, std::function<void()> action)
{
    uint64_t captured_epoch = epoch;
    events.emplace(when, [this, captured_epoch, action = std::move(action)] {
        if (epoch == captured_epoch) {
            action();
        }
    });
    if (!inTick) {
        arm();
    }
}

/** 记录观测事件。 */
void
LinkModel::emit(LinkPoint point, unsigned side, uint64_t cookie,
                unsigned value, const FrameTime &frame,
                const std::vector<uint8_t> &bytes, bool replay,
                const std::vector<uint8_t> &framing)
{
    LinkTrace trace;
    trace.point = point;
    trace.tick = clock.now();
    trace.side = side;
    trace.cookie = cookie;
    trace.value = value;
    trace.replay = replay;
    trace.frame = frame;
    trace.bytes = bytes;
    trace.framing = framing;
    peer.observe(trace);
}

/** 记录错误并通知所属端口。 */
void
LinkModel::report(unsigned side, LinkError error,
                  const std::vector<uint8_t> &header)
{
    emit(LinkPoint::Error, side, 0, unsigned(error), {}, header);
    peer.error(side, error, header);
}

/** 改变当前可观测链路状态。 */
void
LinkModel::setState(LinkState state)
{
    linkState = state;
    emit(LinkPoint::State, 0, 0, unsigned(state));
    peer.stateChanged();
}

/** 指定侧的 VC0 已完成 Data Link 初始化。 */
bool
LinkModel::active(unsigned side) const
{
    return !perst && ports.at(side).phase == FcPhase::Active &&
           linkState != LinkState::Down && linkState != LinkState::HotReset;
}

/** 指定侧累计统计。 */
const LinkCounters &
LinkModel::counters(unsigned side) const
{
    return ports.at(side).stats;
}

/** 指定侧当前 replay 数据存储，单位 B。 */
unsigned
LinkModel::replayBytes(unsigned side) const
{
    return ports.at(side).replay.usedBytes();
}

/** 指定侧当前待发头部数。 */
unsigned
LinkModel::queued(unsigned side, Traffic traffic) const
{
    return ports.at(side).transmit[unsigned(traffic)].size();
}

/** 读取实体资源占用，供适配器和独立检查器核算。 */
LinkResources
LinkModel::resources(unsigned side) const
{
    const auto &port = ports.at(side);
    LinkResources result;
    for (unsigned index = 0; index < 3; ++index) {
        result.txHeaders[index] = port.transmit[index].size();
    }
    result.txBytes = port.transmitBytes;
    result.rxHeaders = port.receiveHeaders;
    result.rxBytes = port.receiveBytes;
    result.replayBytes = port.replay.usedBytes();
    result.replayHeaders = port.replay.size();
    return result;
}

/** 只查询本地准入，尚未初始化的数据链路返回 false。 */
bool
LinkModel::canSubmit(unsigned side, const Tlp &tlp) const
{
    const auto &port = ports.at(side);
    auto traffic = tlp.traffic();
    unsigned index = unsigned(traffic);
    if (!active(side) || frozen ||
        (draining && traffic != Traffic::Completion)) {
        return false;
    }
    return port.transmit[index].size() < headerCapacity(traffic, false) &&
           dataAllocation(tlp) <=
               dataCapacity(traffic, false) - port.transmitBytes[index];
}

/** 接受并复制完整 TLP，成功时为 cookie 为零的请求分配观测编号。 */
bool
LinkModel::submit(unsigned side, Tlp &tlp)
{
    if (!canSubmit(side, tlp)) {
        return false;
    }
    auto &port = ports.at(side);
    std::string error = tlp.validate(port.mps, 512);
    if (!error.empty()) {
        throw std::invalid_argument("outgoing PCIe TLP " + error);
    }
    if (!tlp.cookie) {
        tlp.cookie = nextCookie++;
    }
    unsigned index = unsigned(tlp.traffic());
    port.transmit[index].push_back(
        {tlp, clock.now(), adapterReady(clock.now()), nextOrder++});
    port.transmitBytes[index] += dataAllocation(tlp);
    lastTransaction = clock.now();
    emit(LinkPoint::Accept, side, tlp.cookie, 0, {}, tlp.encode());
    if (linkState == LinkState::L1 || linkState == LinkState::L1Negotiation ||
        linkState == LinkState::L1Entry) {
        wakeLink();
    }
    if (!inTick) {
        arm();
    }
    return true;
}

/** 只撤销尚未进入 DLL 的本地 TLP，已提交时返回 false。 */
bool
LinkModel::cancel(unsigned side, uint64_t cookie)
{
    auto &port = ports.at(side);
    for (unsigned index = 0; index < 3; ++index) {
        auto &queue = port.transmit[index];
        auto entry = std::find_if(queue.begin(), queue.end(),
                                  [cookie](const Queued &queued) {
                                      return queued.tlp.cookie == cookie;
                                  });
        if (entry == queue.end()) {
            continue;
        }
        port.transmitBytes[index] -= dataAllocation(entry->tlp);
        queue.erase(entry);
        emit(LinkPoint::Cancel, side, cookie, 0);
        peer.cancelled(side, cookie, false);
        peer.transmitReady(side);
        return true;
    }
    return false;
}

/** 目标的有限后端可以再次接受，解除接收重试。 */
void
LinkModel::retryReceive()
{
    if (!frozen) {
        clock.wakeAt(clock.now());
    }
}

/** 请求者记录线上完成已全部校验。 */
void
LinkModel::noteComplete(unsigned side, uint64_t cookie)
{
    emit(LinkPoint::Complete, side, cookie);
}

/** 本地超时或隔离结束，停止等待线上完成。 */
void
LinkModel::noteRetired(unsigned side, uint64_t cookie)
{
    emit(LinkPoint::Retired, side, cookie);
}

/** 将事务层检测的错误计入共同 trace 和 AER 回调。 */
void
LinkModel::noteError(unsigned side, LinkError error,
                     const std::vector<uint8_t> &header)
{
    report(side, error, header);
}

/** 目标记录操作或写入已可见。 */
void
LinkModel::noteVisible(unsigned side, uint64_t cookie)
{
    emit(LinkPoint::Visible, side, cookie);
}

/** 改变后续构包的 MPS 和 Extended Synch，缩减前需排空。 */
bool
LinkModel::configure(unsigned side, unsigned mps, bool extended_synch)
{
    auto &port = ports.at(side);
    if (mps != 128 && mps != 256) {
        return false;
    }
    if (mps < port.mps) {
        for (const auto &endpoint : ports) {
            for (const auto &queue : endpoint.transmit) {
                for (const auto &entry : queue) {
                    if (entry.tlp.payload.size() > mps) {
                        return false;
                    }
                }
            }
            for (const auto &entry : endpoint.replay.entries()) {
                Tlp tlp;
                std::string error;
                std::vector<uint8_t> body(entry->packet.begin() + 2,
                                          entry->packet.end() - 4);
                if (!Tlp::decode(body, tlp, error) ||
                    tlp.payload.size() > mps) {
                    return false;
                }
            }
        }
    }
    port.mps = mps;
    port.extendedSynch = extended_synch;
    return true;
}

/** 加入有界 DLLP 队列，累计更新按类型合并。 */
void
LinkModel::queueDllp(unsigned side, const Dllp &dllp)
{
    auto &queue = ports[side].dllps;
    auto same =
        std::find_if(queue.begin(), queue.end(), [&dllp](const Dllp &entry) {
            return entry.type == dllp.type;
        });
    if (same != queue.end()) {
        *same = dllp;
        return;
    }
    if (queue.size() >= pciedefs::TX_DLLP_SLOTS) {
        throw std::logic_error("PCIe finite DLLP queue exhausted");
    }
    queue.push_back(dllp);
}

/** 在该侧形成最新累计 FC 广告。 */
Dllp
LinkModel::creditAdvertisement(unsigned side, unsigned traffic,
                               uint8_t base_type) const
{
    const auto &port = ports[side];
    return Dllp::flowControl(base_type + traffic * 16,
                             port.allocatedHeaders[traffic] & 255,
                             port.allocatedData[traffic] & 4095, 1, 1);
}

/** 对指定侧安排 Feature、InitFC1 或 InitFC2。 */
void
LinkModel::sendInitialization(unsigned side)
{
    auto &port = ports[side];
    if (port.phase == FcPhase::Feature) {
        Dllp feature;
        feature.type = 2;
        feature.data[0] = port.remoteFeature ? 128 : 0;
        feature.data[2] = 1;
        queueDllp(side, feature);
    } else {
        uint8_t type = port.phase == FcPhase::Init1 ? 0x40 : 0xc0;
        for (unsigned traffic = 0; traffic < 3; ++traffic) {
            queueDllp(side, creditAdvertisement(side, traffic, type));
        }
    }
    port.flowDue = clock.now() + pciedefs::FC_INIT_REPEAT_PS;
}

/** 处理接收的有效 DLLP。 */
void
LinkModel::receiveDllp(unsigned side, const std::vector<uint8_t> &bytes)
{
    auto &port = ports[side];
    Dllp dllp;
    if (!Dllp::decode(bytes, dllp)) {
        ++port.stats.crcErrors;
        report(side, LinkError::BadDllp);
        return;
    }
    ++port.stats.dllpReceive;
    emit(LinkPoint::DllpReceive, side, 0, dllp.type, {}, bytes);
    if (dllp.type == 0 || dllp.type == 16) {
        auto ack = port.replay.acknowledge(dllp.sequence());
        if (!ack.valid) {
            report(side, LinkError::DataLinkProtocol);
            return;
        }
        if (ack.progress) {
            port.replayRounds = 0;
            if (linkState == LinkState::L0 ||
                linkState == LinkState::L1Negotiation) {
                port.replayDue = port.replay.size()
                                     ? clock.now() + replayLatency(port)
                                     : Never;
            } else {
                port.replayRemaining =
                    port.replay.size() ? replayLatency(port) : Never;
            }
            for (auto cookie : ack.cookies) {
                emit(LinkPoint::Acknowledge, side, cookie, dllp.sequence());
                peer.acknowledged(side, cookie);
            }
            peer.transmitReady(side);
        }
        if (dllp.type == 16 && port.replay.size()) {
            beginReplay(side, false);
        }
        return;
    }
    if (dllp.type == 2) {
        if (port.phase != FcPhase::Feature) {
            return;
        }
        port.remoteFeature = true;
        port.remoteFeatureAck |= dllp.data[0] & 128;
        if (port.remoteFeatureAck) {
            port.phase = FcPhase::Init1;
            sendInitialization(side);
        } else {
            sendInitialization(side);
        }
        return;
    }
    unsigned group = dllp.type & 0xf0;
    bool initial = group >= 0x40 && group <= 0x60;
    bool confirm = group >= 0xc0 && group <= 0xe0;
    bool update = group >= 0x80 && group <= 0xa0;
    if (initial || confirm || update) {
        if (dllp.type & 15) {
            return;
        }
        unsigned traffic = (group >> 4) & 3;
        bool valid = true;
        if ((initial || confirm) &&
            (port.phase == FcPhase::Feature || port.phase == FcPhase::Init1)) {
            if (!(port.initialMask & (1u << traffic))) {
                valid = port.headers[traffic].initialize(dllp.headerCredit(),
                                                         dllp.headerScale());
                valid &= port.data[traffic].initialize(dllp.dataCredit(),
                                                       dllp.dataScale());
                if (valid) {
                    port.initialMask |= 1u << traffic;
                }
            }
            if (port.initialMask == 7) {
                port.phase = FcPhase::Init2;
                sendInitialization(side);
            }
        }
        if (update && port.phase == FcPhase::Active) {
            valid = port.headers[traffic].update(dllp.headerCredit(),
                                                 dllp.headerScale());
            valid &=
                port.data[traffic].update(dllp.dataCredit(), dllp.dataScale());
        }
        if (!valid) {
            report(side, LinkError::FlowControl);
            return;
        }
        if ((confirm || update) && port.phase == FcPhase::Init2) {
            port.phase = FcPhase::Active;
            port.flowDue = clock.now() + pciedefs::FC_UPDATE_PS;
            peer.stateChanged();
            peer.transmitReady(side);
        }
        return;
    }
    if (dllp.type == 0x23 || dllp.type == 0x20) {
        if (side != 0 || linkState != LinkState::L1Negotiation) {
            return;
        }
        if (!powerAccepted) {
            if (powerRejected) {
                return;
            }
            bool pending = false;
            for (const auto &queue : port.transmit) {
                pending |= !queue.empty();
            }
            bool ack_pending = port.ackCount || port.nakScheduled;
            if (pending || ack_pending || (!pciPower && !port.aspm)) {
                Tlp nak;
                nak.kind = TlpKind::Message;
                nak.messageCode = 0x14;
                nak.messageRouting = 4;
                if (!submit(0, nak)) {
                    return;
                }
                powerRejected = true;
                port.blockTlp = false;
                return;
            }
            port.blockTlp = true;
            powerAccepted = true;
        }
        return;
    }
    if (dllp.type == 0x24 && side == 1 &&
        linkState == LinkState::L1Negotiation) {
        powerAck = true;
    }
}

/** 安排累计 ACK 或 NAK。 */
void
LinkModel::acknowledgeReceive(unsigned side, bool nak, bool immediate)
{
    auto &port = ports[side];
    if (nak) {
        if (port.nakScheduled) {
            return;
        }
        port.nakScheduled = true;
    }
    if (nak || immediate || port.ackCount >= pciedefs::ACK_BATCH_TLPS) {
        queueDllp(side, Dllp::acknowledge((port.nextReceive - 1) & 4095, nak));
        port.ackCount = 0;
        port.ackDue = Never;
    } else if (port.ackDue == Never) {
        // 当前包不可抢占，提前保留四 DWORD 头、framing 和最坏块头。
        uint64_t symbols = (port.mps + 24) / actualWidth;
        uint64_t bits = actualGeneration < 3
                            ? symbols * 10
                            : symbols * 8 + 2 * ((symbols + 30) / 16);
        uint64_t margin = (bits * port.wire.state().bitHalf + 1) / 2 +
                          port.wire.state().bitHalf * 5;
        port.ackDue = clock.now() + ackLatency(port) - margin;
    }
}

/** 处理收到的 TLP、完整性与重复抑制。 */
void
LinkModel::receiveTlp(unsigned side, uint64_t cookie,
                      const std::vector<uint8_t> &bytes, bool framing_error)
{
    if (linkState != LinkState::L0 && linkState != LinkState::L1Negotiation) {
        return;
    }
    auto &port = ports[side];
    if (framing_error) {
        report(side, LinkError::Receiver);
        recoveryRequested = true;
        return;
    }
    uint16_t sequence = 0;
    std::vector<uint8_t> body;
    if (!checkDataLinkPacket(bytes, sequence, body)) {
        ++port.stats.crcErrors;
        report(side, LinkError::BadTlp);
        acknowledgeReceive(side, true, true);
        return;
    }
    if (sequence != port.nextReceive) {
        if (((port.nextReceive - sequence) & 4095) <= 2048) {
            ++port.stats.duplicateReceive;
            acknowledgeReceive(side, false, true);
        } else {
            report(side, LinkError::BadTlp);
            acknowledgeReceive(side, true, true);
        }
        return;
    }
    port.nextReceive = (port.nextReceive + 1) & 4095;
    port.nakScheduled = false;
    ++port.ackCount;
    acknowledgeReceive(side, false, false);
    Tlp tlp;
    std::string error;
    if (!Tlp::decode(body, tlp, error)) {
        report(side, LinkError::Malformed, body);
        return;
    }
    tlp.cookie = cookie;
    unsigned index = unsigned(tlp.traffic());
    unsigned allocated = dataAllocation(tlp);
    if (port.receiveHeaders[index] == headerCapacity(tlp.traffic(), true) ||
        allocated >
            dataCapacity(tlp.traffic(), true) - port.receiveBytes[index]) {
        report(side, LinkError::ReceiverOverflow, body);
        return;
    }
    ++port.receiveHeaders[index];
    port.receiveBytes[index] += allocated;
    auto received = std::make_shared<Received>();
    received->tlp = tlp;
    received->epoch = epoch;
    received->ready = adapterReady(clock.now());
    port.receive.push_back(received);
    emit(LinkPoint::Receive, side, cookie, sequence, {}, bytes);
    if (!tlp.validate(port.mps, 512).empty()) {
        report(side, LinkError::Malformed, body);
        received->delivered = true;
        releaseReceive(side, received);
        return;
    }
    ++port.stats.tlpReceive;
    port.stats.payloadReceive += tlp.payload.size();
    lastTransaction = clock.now();
    if (tlp.kind == TlpKind::Message && tlp.messageCode == 0x14 && side == 1 &&
        linkState == LinkState::L1Negotiation && !powerAccepted) {
        received->delivered = true;
        releaseReceive(side, received);
        ports[0].blockTlp = false;
        ports[1].blockTlp = false;
        powerRetry = clock.now() + pciedefs::L1_REJECT_RETRY_PS;
        stateDue = Never;
        setState(LinkState::L0);
    }
}

/** RX 处理者释放一次实体缓冲并安排累计信用归还。 */
void
LinkModel::releaseReceive(unsigned side,
                          const std::shared_ptr<Received> &received)
{
    if (received->epoch != epoch) {
        return;
    }
    if (received->released) {
        throw std::logic_error("PCIe receive buffer released twice");
    }
    received->released = true;
    at(adapterReady(clock.now()), [this, side, received] {
        auto &port = ports[side];
        unsigned index = unsigned(received->tlp.traffic());
        --port.receiveHeaders[index];
        port.receiveBytes[index] -= dataAllocation(received->tlp);
        auto entry =
            std::find(port.receive.begin(), port.receive.end(), received);
        if (entry == port.receive.end()) {
            throw std::logic_error("PCIe receive owner missing");
        }
        port.receive.erase(entry);
        if (index < 2) {
            ++port.allocatedHeaders[index];
            port.allocatedData[index] +=
                (received->tlp.payload.size() + 15) / 16;
            if (linkState != LinkState::L1) {
                queueDllp(side, creditAdvertisement(side, index, 0x80));
            }
        }
    });
}

/** 将有限 RX 缓冲按顺序交给可接收的目标。 */
void
LinkModel::deliverReceive(unsigned side)
{
    auto &port = ports[side];
    for (size_t index = 0; index < port.receive.size(); ++index) {
        auto received = port.receive[index];
        if (received->delivered || received->ready > clock.now()) {
            continue;
        }
        bool blocked = false;
        for (size_t older = 0; older < index; ++older) {
            if (!port.receive[older]->delivered &&
                !mayPass(received->tlp, port.receive[older]->tlp)) {
                blocked = true;
            }
        }
        if (blocked) {
            continue;
        }
        uint64_t before = epoch;
        bool accepted =
            peer.receive(side, received->tlp, [this, side, received] {
                releaseReceive(side, received);
            });
        if (before != epoch) {
            return;
        }
        if (accepted) {
            received->delivered = true;
        }
    }
}

/** 从有界待发队列选取符合排序与资源条件的 TLP。 */
int
LinkModel::selectTraffic(unsigned side)
{
    auto &port = ports[side];
    for (unsigned turn = 0; turn < 3; ++turn) {
        unsigned index = (port.roundRobin + turn) % 3;
        if (port.transmit[index].empty()) {
            continue;
        }
        const auto &candidate = port.transmit[index].front();
        if (candidate.ready > clock.now()) {
            continue;
        }
        bool blocked = false;
        for (const auto &queue : port.transmit) {
            if (!queue.empty() && queue.front().order < candidate.order &&
                !mayPass(candidate.tlp, queue.front().tlp)) {
                blocked = true;
            }
        }
        if (blocked) {
            continue;
        }
        if (!port.headers[index].canSpend(1) ||
            !port.data[index].canSpend((candidate.tlp.payload.size() + 15) /
                                       16)) {
            ++port.stats.creditStalls;
            continue;
        }
        if (!port.replay.canAppend(candidate.tlp.headerBytes() +
                                   candidate.tlp.payload.size())) {
            ++port.stats.replayStalls;
            continue;
        }
        port.roundRobin = (index + 1) % 3;
        return index;
    }
    return -1;
}

/** 消耗指定的单次数字故障。 */
bool
LinkModel::takeFault(unsigned side, LinkFault fault)
{
    auto &count = ports[side].faults[unsigned(fault)];
    if (!count) {
        return false;
    }
    --count;
    return true;
}

/** 在指定发送方向注入有限次数的数字故障。 */
void
LinkModel::inject(unsigned side, LinkFault fault, unsigned count)
{
    if (fault == LinkFault::Count ||
        UINT32_MAX - ports.at(side).faults[unsigned(fault)] < count) {
        throw std::invalid_argument("PCIe fault count");
    }
    ports[side].faults[unsigned(fault)] += count;
}

/** 发送一个 DLLP，保留独立的反向传播。 */
void
LinkModel::transmitDllp(unsigned side, const Dllp &dllp)
{
    auto &port = ports[side];
    auto bytes = dllp.encode();
    if (takeFault(side, LinkFault::DllpCrc)) {
        bytes.back() ^= 1;
    }
    auto frame = port.wire.packet(8, clock.now());
    port.busyUntil = frame.endTick();
    ++port.stats.dllpTransmit;
    port.stats.packetHalf += 8 / actualWidth *
                                 (actualGeneration < 3 ? 10 : 8) *
                                 port.wire.state().bitHalf +
                             frame.encodingHalf;
    emit(LinkPoint::DllpTransmit, side, 0, dllp.type, frame, bytes);
    bool dropped = dllp.type == 0 && takeFault(side, LinkFault::DropAck);
    if ((dllp.type & 0xf0) >= 0x80 && (dllp.type & 0xf0) <= 0xa0) {
        dropped |= takeFault(side, LinkFault::DropUpdate);
    }
    if (!dropped) {
        at(frame.endTick() + config.propagationPs,
           [this, side, bytes] { receiveDllp(side ^ 1, bytes); });
    }
}

/** 发送一个原始或重放的不可变副本。 */
void
LinkModel::transmitTlp(unsigned side,
                       const std::shared_ptr<const ReplayWindow::Entry> &entry,
                       bool replay)
{
    auto &port = ports[side];
    auto bytes = entry->packet;
    bool framing_error = false;
    std::vector<uint8_t> framing;
    if (actualGeneration >= 3) {
        auto token = startToken((bytes.size() + 2) / 4, entry->sequence);
        if (takeFault(side, LinkFault::Framing)) {
            token[1] ^= 1;
        }
        unsigned length = 0;
        uint16_t sequence = 0;
        framing_error = !checkStartToken(token, length, sequence) ||
                        length * 4 != bytes.size() + 2 ||
                        sequence != entry->sequence;
        framing.assign(token.begin(), token.end());
    }
    if (takeFault(side, LinkFault::TlpCrc)) {
        bytes.back() ^= 1;
    }
    unsigned wire_bytes = bytes.size() + 2;
    auto frame = port.wire.packet(wire_bytes, clock.now());
    port.busyUntil = frame.endTick();
    port.stats.packetHalf += wire_bytes / actualWidth *
                                 (actualGeneration < 3 ? 10 : 8) *
                                 port.wire.state().bitHalf +
                             frame.encodingHalf;
    if (replay) {
        ++port.stats.replayTransmit;
    }
    if (port.replayDue == Never) {
        port.replayDue = frame.endTick() + replayLatency(port) + 1;
    }
    emit(LinkPoint::Transmit, side, entry->cookie, entry->sequence, frame,
         bytes, replay, framing);
    if (!takeFault(side, LinkFault::DropTlp)) {
        at(frame.endTick() + config.propagationPs,
           [this, side, entry, bytes, framing_error] {
               receiveTlp(side ^ 1, entry->cookie, bytes, framing_error);
           });
    }
    if (!replay && takeFault(side, LinkFault::Duplicate)) {
        port.replays.push_back(entry->sequence);
    }
}

/** 启动一次保留原包、信用和序号的重放。 */
void
LinkModel::beginReplay(unsigned side, bool timeout)
{
    auto &port = ports[side];
    if (!port.replay.size() || !port.replays.empty()) {
        return;
    }
    if (timeout) {
        report(side, LinkError::ReplayTimeout);
    }
    port.replayDue = Never;
    for (const auto &entry : port.replay.entries()) {
        port.replays.push_back(entry->sequence);
    }
    port.replayRounds = (port.replayRounds + 1) & 3;
    if (!port.replayRounds) {
        report(side, LinkError::ReplayRollover);
        recoveryRequested = true;
    }
}

/** 一个方向在当前边界选择 DLLP、SKP、replay 或新 TLP。 */
void
LinkModel::transmit(unsigned side)
{
    auto &port = ports[side];
    if (port.busyUntil > clock.now() || port.idleSent ||
        (linkState != LinkState::L0 &&
         linkState != LinkState::L1Negotiation)) {
        return;
    }
    // 紧急 ACK/NAK 先发送，其余 DLLP 保留 FIFO，相同类型已合并。
    auto urgent =
        std::find_if(port.dllps.begin(), port.dllps.end(),
                     [](const Dllp &dllp) { return dllp.type == 16; });
    if (urgent == port.dllps.end()) {
        urgent = std::find_if(port.dllps.begin(), port.dllps.end(),
                              [](const Dllp &dllp) { return dllp.type == 0; });
    }
    if (urgent != port.dllps.end()) {
        Dllp dllp = *urgent;
        port.dllps.erase(urgent);
        transmitDllp(side, dllp);
        return;
    }
    if (port.wire.nextSkpTick() <= clock.now()) {
        auto frame = port.wire.skip(clock.now());
        port.busyUntil = frame.endTick();
        port.stats.maintenanceHalf += frame.endHalf - frame.beginHalf;
        emit(LinkPoint::Skip, side, 0,
             port.wire.state().controlSkpNext ? 0 : 1, frame);
        return;
    }
    if (!port.dllps.empty()) {
        Dllp dllp = port.dllps.front();
        port.dllps.pop_front();
        transmitDllp(side, dllp);
        return;
    }
    while (!port.replays.empty()) {
        auto entry = port.replay.find(port.replays.front());
        port.replays.pop_front();
        if (entry) {
            transmitTlp(side, entry, true);
            return;
        }
    }
    if (port.blockTlp || port.phase != FcPhase::Active) {
        return;
    }
    int selected = selectTraffic(side);
    if (selected < 0) {
        return;
    }
    auto queued = port.transmit[selected].front();
    port.transmit[selected].pop_front();
    port.transmitBytes[selected] -= dataAllocation(queued.tlp);
    port.headers[selected].spend(1);
    port.data[selected].spend((queued.tlp.payload.size() + 15) / 16);
    auto entry = port.replay.append(queued.tlp);
    if (!entry) {
        throw std::logic_error("PCIe replay admission changed");
    }
    ++port.stats.tlpTransmit;
    port.stats.payloadTransmit += queued.tlp.payload.size();
    port.stats.queueWaitPs += clock.now() - queued.accepted;
    lastTransaction = clock.now();
    emit(LinkPoint::Commit, side, entry->cookie, entry->sequence);
    peer.committed(side, entry->cookie);
    transmitTlp(side, entry, false);
    peer.transmitReady(side);
}

/** 运行当前时刻的事件、协议期限和仲裁。 */
void
LinkModel::tick()
{
    if (frozen) {
        return;
    }
    inTick = true;
    while (!events.empty() && events.begin()->first <= clock.now()) {
        auto action = std::move(events.begin()->second);
        events.erase(events.begin());
        action();
    }
    if (stateDue <= clock.now()) {
        advanceTraining();
    }
    bool sending =
        linkState == LinkState::L0 || linkState == LinkState::L1Negotiation;
    if (sending) {
        for (unsigned side = 0; side < 2; ++side) {
            auto &port = ports[side];
            if (port.ackDue <= clock.now()) {
                acknowledgeReceive(side, false, true);
            }
            if (port.replayDue <= clock.now()) {
                beginReplay(side, true);
            }
            if (port.flowDue <= clock.now()) {
                if (port.phase != FcPhase::Active) {
                    sendInitialization(side);
                } else {
                    queueDllp(side, creditAdvertisement(side, 0, 0x80));
                    queueDllp(side, creditAdvertisement(side, 1, 0x80));
                    port.flowDue =
                        clock.now() + (port.extendedSynch
                                           ? pciedefs::FC_UPDATE_EXTENDED_PS
                                           : pciedefs::FC_UPDATE_PS);
                }
            }
            deliverReceive(side);
        }
        if (recoveryRequested && linkState == LinkState::L0 &&
            ports[0].busyUntil <= clock.now() &&
            ports[1].busyUntil <= clock.now()) {
            beginRecovery();
        }
        progressPower();
        transmit(0);
        transmit(1);
    } else {
        deliverReceive(0);
        deliverReceive(1);
        progressPower();
    }
    if (draining && empty() && peer.quiescent() &&
        (linkState == LinkState::L0 || linkState == LinkState::L1)) {
        frozen = true;
        frozenAt = clock.now();
        emit(LinkPoint::Drained, 0);
        peer.drained();
    }
    inTick = false;
    arm();
}

/** 计算下次需要唤醒的事件和协议期限。 */
void
LinkModel::arm()
{
    if (frozen || inTick) {
        return;
    }
    uint64_t next = stateDue;
    if (!events.empty()) {
        next = std::min(next, events.begin()->first);
    }
    bool sending =
        linkState == LinkState::L0 || linkState == LinkState::L1Negotiation;
    for (const auto &port : ports) {
        if (port.busyUntil > clock.now()) {
            next = std::min(next, port.busyUntil);
        }
        for (const auto &received : port.receive) {
            if (!received->delivered && received->ready > clock.now()) {
                next = std::min(next, received->ready);
            }
        }
        if (!sending || port.idleSent) {
            continue;
        }
        for (const auto &queue : port.transmit) {
            if (!queue.empty() && queue.front().ready > clock.now()) {
                next = std::min(next, queue.front().ready);
            }
        }
        for (uint64_t due : {port.ackDue, port.replayDue, port.flowDue,
                             port.wire.nextSkpTick()}) {
            if (due != Never) {
                next = std::min(next, std::max(due, port.busyUntil));
            }
        }
        if (!port.dllps.empty() || !port.replays.empty()) {
            next = std::min(next, std::max(clock.now(), port.busyUntil));
        }
    }
    if (linkState == LinkState::L0 && !draining && canEnterL1()) {
        next = std::min(
            next, std::max(powerRetry,
                           d3hot ? clock.now()
                                 : lastTransaction + pciedefs::ASPM_IDLE_PS));
    }
    if (recoveryRequested && linkState == LinkState::L0) {
        next = std::min(next, std::max({clock.now(), ports[0].busyUntil,
                                        ports[1].busyUntil}));
    }
    if (next != Never) {
        clock.wakeAt(std::max(next, clock.now()));
    }
}

/** 所有本地缓冲、重放和传输事件均已结算。 */
bool
LinkModel::empty() const
{
    if (!events.empty()) {
        return false;
    }
    for (const auto &port : ports) {
        if (port.busyUntil > clock.now() || port.replay.size() ||
            !port.replays.empty() || !port.receive.empty() ||
            !port.dllps.empty() || port.ackCount) {
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

/** 停止维护流量前先排空全部已接受事务。 */
void
LinkModel::drain()
{
    draining = true;
    if (!frozen) {
        clock.wakeAt(clock.now());
    }
}

/** 排空恢复后继续维护流量和正常准入。 */
void
LinkModel::resume()
{
    uint64_t elapsed = frozen ? clock.now() - frozenAt : 0;
    draining = false;
    frozen = false;
    for (auto &port : ports) {
        auto state = port.wire.state();
        state.endHalf += elapsed * 2;
        state.dataOriginHalf += elapsed * 2;
        state.nextSkpHalf += elapsed * 2;
        port.wire.restore(state);
        port.busyUntil += elapsed;
        if (port.flowDue != Never) {
            port.flowDue += elapsed;
        }
    }
    lastTransaction += elapsed;
    powerRetry += elapsed;
    clock.wakeAt(clock.now());
}

} // namespace gem5::gw100::pcie
