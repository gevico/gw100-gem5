/**
 * @file  memory_tile.cc
 * @brief Gw100MemoryTile 的实现
 *
 * @see spec-dram, spec-clock-reset-power
 */

#include "src/gw100/mem_tile/memory_tile.hh"

#include <algorithm>
#include <cstring>
#include "base/trace.hh"
#include "debug/Gw100Memory.hh"
#include "mem/mem_interface.hh"

#include "src/gw100/mem_tile/memory_ingress.hh"

namespace gem5::gw100
{

namespace md = memorydefs;

/** 绑定所属 Tile */
Gw100MemoryTile::FrontPort::FrontPort(const std::string &name,
                                      Gw100MemoryTile &tile)
    : ResponsePort(name), tile(tile)
{}

/** 返回窗口 */
AddrRangeList
Gw100MemoryTile::FrontPort::getAddrRanges() const
{
    return {AddrRange(0, md::TOTAL_BYTES, 10, 0, 2, tile.control.tile),
            AddrRange(tile.registerBase(),
                      tile.registerBase() + md::REGISTER_BYTES)};
}

/** 接受 timing 请求或反压 */
bool
Gw100MemoryTile::FrontPort::recvTimingReq(PacketPtr pkt)
{ return tile.receive(pkt); }

/** 调试只读控制寄存器 */
void
Gw100MemoryTile::FrontPort::recvFunctional(PacketPtr pkt)
{
    if (pkt->isRead() && pkt->getAddr() >= tile.registerBase()) {
        uint64_t value = 0;
        if (tile.control.read(pkt->getAddr() - tile.registerBase(),
                              pkt->getSize(), value)) {
            pkt->setUintX(value, ByteOrder::little);
            pkt->makeResponse();
            return;
        }
    }
    pkt->makeResponse();
    pkt->setBadAddress();
}

/** 精确配置拒绝 atomic 数据路径 */
Tick
Gw100MemoryTile::FrontPort::recvAtomic(PacketPtr pkt)
{ panic("%s: precise Memory Tile requires timing requests", name()); }

/** 返回路径恢复 */
void
Gw100MemoryTile::FrontPort::recvRespRetry()
{
    tile.replyBlocked = false;
    for (auto &link : tile.links) {
        link->retryResponse();
    }
    tile.wake();
}

/** 绑定所属 Tile */
Gw100MemoryTile::MediaPort::MediaPort(const std::string &name,
                                      Gw100MemoryTile &tile)
    : RequestPort(name), tile(tile)
{}

/** 发出媒体包，满时持有重试 */
void
Gw100MemoryTile::MediaPort::send(PacketPtr pkt)
{
    if (waiting.empty() && sendTimingReq(pkt)) {
        return;
    }
    waiting.push_back(pkt);
}

/** 返回媒体结果 */
bool
Gw100MemoryTile::MediaPort::recvTimingResp(PacketPtr pkt)
{
    tile.mediaResponse(pkt);
    return true;
}

/** 原生控制器撤销反压 */
void
Gw100MemoryTile::MediaPort::recvReqRetry()
{
    while (!waiting.empty() && sendTimingReq(waiting.front())) {
        waiting.pop_front();
    }
    tile.wake();
}

/** 绑定两个原生控制器和 DDR 时钟 */
Gw100MemoryTile::Gw100MemoryTile(const Params &p)
    : ClockedObject(p),
      control(p.tile_id),
      front(name() + ".cpu_side", *this),
      event([this] { process(); }, name() + ".process"),
      calibrationLatency(p.calibration_latency),
      trainingLatency(p.training_latency),
      failMask(p.fail_mask),
      timeoutMask(p.timeout_mask),
      faultPhase(p.fault_phase),
      corruptSelftest(p.corrupt_selftest),
      selftestMask(p.selftest_fail_mask)
{
    fatal_if(p.tile_id >= md::TILES || p.controllers.size() != 2 ||
                 p.port_mem_side_connection_count != 2,
             "%s: Memory Tile needs two channels", name());
    fatal_if((p.fail_mask | p.timeout_mask | p.selftest_fail_mask) >> 16 ||
                 (p.fault_phase != md::CMD_CALIBRATE &&
                  p.fault_phase != md::CMD_TRAIN),
             "%s: invalid memory fault configuration", name());
    selftestMask = (selftestMask >> (p.tile_id * 4)) & 15;
    for (unsigned ch = 0; ch < 2; ++ch) {
        controllers[ch] = p.controllers[ch];
        media[ch] = std::make_unique<MediaPort>(
            name() + ".mem_side[" + std::to_string(ch) + "]", *this);
        links[ch] = std::make_unique<MemoryLink>(
            *this, *p.clk_domain, *p.ddr_domain,
            name() + ".channel" + std::to_string(ch), md::FRONT_SLOTS);
        links[ch]->onProgress = [this] { wake(); };
        links[ch]->onTargetProgress = [this, ch] { processChannel(ch); };
    }
    control.onCommand = [this](unsigned ch, unsigned cmd) {
        beginCommand(ch, cmd);
    };
}

/** 查找控制或数据端口 */
Port &
Gw100MemoryTile::getPort(const std::string &name, PortID idx)
{
    if (name == "cpu_side") {
        return front;
    }
    if (name == "mem_side" && idx >= 0 && idx < 2) {
        return *media[idx];
    }
    return ClockedObject::getPort(name, idx);
}

/** 发布数据和控制窗口 */
void
Gw100MemoryTile::init()
{
    fatal_if(!front.isConnected(), "%s: unconnected Memory Tile", name());
    front.sendRangeChange();
}

/** 该 Tile 的控制寄存器基址 */
Addr
Gw100MemoryTile::registerBase() const
{
    return md::TILE_WINDOW_BASE + control.tile * md::TILE_WINDOW_STRIDE +
           md::REGISTER_OFFSET;
}

/** 注册硬件入口，用于整个子系统的排空 */
void
Gw100MemoryTile::attachIngress(Gw100MemoryIngress *ingress)
{ ingresses.push_back(ingress); }

/** 请求外壳继续处理 */
void
Gw100MemoryTile::wake(Tick when)
{
    when = std::max(clockEdge(Cycles(1)), when);
    if (!event.scheduled()) {
        schedule(event, when);
    } else if (when < event.when()) {
        reschedule(event, when);
    }
}

/** 调试读取控制快照 */
uint64_t
Gw100MemoryTile::readRegister(uint64_t offset) const
{
    uint64_t value = 0;
    unsigned size = 4;
    if (offset == md::REG_EPOCH || offset == md::REG_ERROR_ADDR ||
        offset == md::REG_ERROR_TXN || offset == md::REG_ERROR_INFO ||
        (offset >= md::REG_REQUESTS && offset <= md::REG_TRANSFER_TICKS)) {
        size = 8;
    }
    if (offset >= md::CHANNEL_BASE) {
        unsigned reg = (offset - md::CHANNEL_BASE) % md::CHANNEL_STRIDE;
        if (reg == md::CH_SEQUENCE || reg == md::CH_CK_PS) {
            size = 8;
        }
    }
    if (offset >= md::PROTECTION_BASE &&
        offset < md::PROTECTION_BASE +
                     md::PROTECTION_ENTRIES * md::PROTECTION_STRIDE &&
        (offset - md::PROTECTION_BASE) % md::PROTECTION_STRIDE < 16) {
        size = 8;
    }
    control.read(offset, size, value);
    return value;
}

/** 记录入口预检拒绝的完整事务 */
void
Gw100MemoryTile::recordError(unsigned error, unsigned source, Addr address,
                             uint64_t transaction)
{ control.log(error, source, address, transaction); }

/** 检查请求，已准入事务可在排空期间完成后续子请求 */
unsigned
Gw100MemoryTile::checkAccess(Addr addr, unsigned size, unsigned source,
                             bool admitted) const
{
    if (!size || addr >= md::TOTAL_BYTES || size > md::TOTAL_BYTES - addr) {
        return md::ERR_RANGE;
    }
    if (!control.permitted(addr, size, source)) {
        return md::ERR_PERMISSION;
    }
    Addr end = addr + size;
    for (Addr a = addr; a < end;) {
        unsigned tile = (a >> 9) & 3;
        unsigned ch = (a >> 8) & 1;
        if (tile == control.tile) {
            unsigned state = control.channels[ch].state;
            if (admitted && state == md::STATE_QUIESCING) {
                state = control.channels[ch].runState;
            }
            if (!control.released || !control.pllLocked ||
                (state != md::STATE_READY &&
                 !(source == md::SOURCE_MGMT && state == md::STATE_TEST))) {
                return md::ERR_NOT_READY;
            }
        }
        a = std::min(end, (a | (md::INTERLEAVE_BYTES - 1)) + 1);
    }
    return md::ERR_NONE;
}

/** 返回一个明确错误 */
void
Gw100MemoryTile::reject(PacketPtr pkt, unsigned error)
{
    auto *st = pkt->findNextSenderState<MemorySourceState>();
    control.log(error, st ? st->source : md::SOURCE_BAR2, pkt->getAddr(),
                st ? st->transaction : 0);
    if (pkt->isRead()) {
        std::memset(pkt->getPtr<uint8_t>(), 0xFF, pkt->getSize());
    }
    pkt->makeResponse();
    pkt->setBadCommand();
    replies.push_back({pkt, clockEdge(Cycles(1))});
    wake();
}

/** 处理前端请求 */
bool
Gw100MemoryTile::receive(PacketPtr pkt)
{
    DPRINTF(Gw100Memory, "receive %s %#x\n", pkt->cmdString(), pkt->getAddr());
    auto *st = pkt->findNextSenderState<MemorySourceState>();
    bool mmio = pkt->getAddr() >= registerBase() &&
                pkt->getAddr() - registerBase() < md::REGISTER_BYTES;
    if (replies.size() + controls.size() >= md::FRONT_SLOTS) {
        requestRetry = true;
        ++control.counters[3];
        return false;
    }
    if (!st || !st->valid || !st->valid()) {
        reject(pkt, md::ERR_RESET);
        return true;
    }
    if (mmio) {
        Tick delay = pkt->headerDelay + pkt->payloadDelay;
        pkt->headerDelay = pkt->payloadDelay = 0;
        Tick ready = clockEdge(Cycles(divCeil(delay, clockPeriod()) + 1));
        controls.push_back({pkt, ready});
        wake();
        return true;
    }
    unsigned error =
        checkAccess(pkt->getAddr(), pkt->getSize(), st->source, st->admitted);
    if (error || st->epoch != control.epoch) {
        reject(pkt, error ? error : md::ERR_RESET);
        return true;
    }
    unsigned ch = (pkt->getAddr() >> 8) & 1;
    if (!links[ch]->canSubmit() || restoring[ch] || drainHeld[ch] ||
        !controllers[ch]->mediaAvailable()) {
        requestRetry = true;
        ++control.counters[3];
        return false;
    }
    auto txn = std::make_shared<DataTxn>();
    txn->channel = ch;
    txn->epoch = control.epoch;
    txn->burst = pkt->getAddr() & ~(md::BURST_BYTES - 1);
    txn->rmw = pkt->isWrite() && (pkt->getSize() != md::BURST_BYTES ||
                                  pkt->getAddr() != txn->burst);
    txn->item = std::make_shared<MemoryLink::Item>();
    txn->item->packet = pkt;
    txn->item->availableAt = curTick() + pkt->headerDelay + pkt->payloadDelay;
    pkt->headerDelay = pkt->payloadDelay = 0;
    txn->item->receive = [this, txn] { waiting[txn->channel].push_back(txn); };
    txn->item->complete = [this, txn] {
        return sendResponse(txn->item->packet);
    };
    ++control.counters[0];
    control.counters[pkt->isWrite() ? 2 : 1] += pkt->getSize();
    links[ch]->submit(txn->item);
    return true;
}

/** 在请求实际到达后执行控制寄存器访问 */
void
Gw100MemoryTile::accessControl(PacketPtr pkt)
{
    auto *st = pkt->findNextSenderState<MemorySourceState>();
    unsigned error = md::ERR_NONE;
    uint64_t value = 0;
    if (!st || !st->valid || !st->valid()) {
        error = md::ERR_RESET;
    } else if (st->source != md::SOURCE_MGMT) {
        error = md::ERR_PERMISSION;
    } else if ((pkt->getSize() != 4 && pkt->getSize() != 8) ||
               pkt->getAddr() % pkt->getSize()) {
        error = md::ERR_PROTOCOL;
    } else if (pkt->isWrite()) {
        error = control.write(pkt->getAddr() - registerBase(), pkt->getSize(),
                              pkt->getUintX(ByteOrder::little));
    } else if (!control.read(pkt->getAddr() - registerBase(), pkt->getSize(),
                             value)) {
        error = md::ERR_PROTOCOL;
    } else {
        pkt->setUintX(value, ByteOrder::little);
    }
    if (error) {
        reject(pkt, error);
    } else {
        pkt->makeResponse();
        replies.push_back({pkt, curTick()});
    }
}

/** 检查旧代次是否仍有效 */
bool
Gw100MemoryTile::valid(const std::shared_ptr<DataTxn> &txn) const
{
    auto *st = txn->item->packet->findNextSenderState<MemorySourceState>();
    return txn->epoch == control.epoch && st && st->valid && st->valid();
}

/** 发出一项原生读或写 */
void
Gw100MemoryTile::issue(const std::shared_ptr<DataTxn> &txn, bool rmw_read,
                       const uint8_t *data)
{
    PacketPtr orig = txn->item->packet;
    bool write = orig->isWrite() && !rmw_read;
    auto req = std::make_shared<Request>(txn->burst, md::BURST_BYTES, 0,
                                         orig->req->requestorId());
    auto *pkt = new Packet(req, write ? MemCmd::WriteReq : MemCmd::ReadReq);
    pkt->allocate();
    if (write) {
        pkt->setData(data ? data : orig->getConstPtr<uint8_t>());
    }
    auto *st = new MediaState;
    st->txn = txn;
    st->rmwRead = rmw_read;
    pkt->pushSenderState(st);
    DPRINTF(Gw100Memory, "media-issue %s %#x channel %u\n", pkt->cmdString(),
            pkt->getAddr(), txn->channel);
    media[txn->channel]->send(pkt);
}

/** 返回一个数据操作并释放 burst 锁 */
void
Gw100MemoryTile::finish(const std::shared_ptr<DataTxn> &txn, bool ok)
{
    PacketPtr orig = txn->item->packet;
    if (!ok && orig->isRead()) {
        std::memset(orig->getPtr<uint8_t>(), 0xFF, orig->getSize());
    }
    orig->makeResponse();
    if (!ok) {
        orig->setBadCommand();
    }
    lockedBursts[txn->channel].erase(txn->burst);
    links[txn->channel]->respond(txn->item);
    links[txn->channel]->wakeTarget();
    wake();
}

/** 完成原生媒体访问 */
void
Gw100MemoryTile::mediaResponse(PacketPtr pkt)
{
    DPRINTF(Gw100Memory, "media-response %s %#x\n", pkt->cmdString(),
            pkt->getAddr());
    auto *st = dynamic_cast<MediaState *>(pkt->popSenderState());
    panic_if(!st, "%s: missing media state", name());
    auto txn = st->txn;
    bool ok = !pkt->isError() && valid(txn);
    PacketPtr orig = txn->item->packet;
    if (ok && st->rmwRead) {
        std::array<uint8_t, md::BURST_BYTES> data;
        std::memcpy(data.data(), pkt->getConstPtr<uint8_t>(), data.size());
        std::memcpy(data.data() + orig->getAddr() - txn->burst,
                    orig->getConstPtr<uint8_t>(), orig->getSize());
        issue(txn, false, data.data());
    } else {
        if (ok && orig->isRead()) {
            std::memcpy(orig->getPtr<uint8_t>(),
                        pkt->getConstPtr<uint8_t>() + orig->getAddr() -
                            txn->burst,
                        orig->getSize());
            if ((corruptSelftest || selftestMask) &&
                control.channels[txn->channel].state == md::STATE_TEST) {
                auto *iface = static_cast<const MemCtrlParams &>(
                                  controllers[txn->channel]->params())
                                  .dram;
                const auto &geometry =
                    static_cast<const MemInterfaceParams &>(iface->params());
                unsigned rank =
                    iface->getCtrlAddr(orig->getAddr()) /
                    (geometry.device_rowbuffer_size *
                     geometry.devices_per_rank * geometry.banks_per_rank) %
                    geometry.ranks_per_channel;
                uint64_t mask = 1ULL << (txn->channel * 2 + rank);
                if (corruptSelftest || (selftestMask & mask)) {
                    orig->getPtr<uint8_t>()[0] ^= 1;
                    corruptSelftest = false;
                    selftestMask &= ~mask;
                }
            }
        }
        finish(txn, ok);
    }
    delete st;
    delete pkt;
}

/** 执行一个已接受的异步命令 */
void
Gw100MemoryTile::beginCommand(unsigned ch, unsigned cmd)
{
    if (cmd == md::CMD_CALIBRATE || cmd == md::CMD_TRAIN) {
        uint64_t epoch = control.epoch;
        training[ch] = std::make_unique<EventFunctionWrapper>(
            [this, ch, cmd, epoch] { feedback(ch, cmd, epoch); },
            name() + ".feedback" + std::to_string(ch));
        schedule(*training[ch],
                 curTick() + (cmd == md::CMD_CALIBRATE ? calibrationLatency
                                                       : trainingLatency));
    } else {
        pendingCommand[ch] = cmd;
        wake();
    }
}

/** 取得校准或训练反馈 */
void
Gw100MemoryTile::feedback(unsigned ch, unsigned cmd, uint64_t epoch)
{
    if (epoch != control.epoch) {
        return;
    }
    unsigned shift = (control.tile * 2 + ch) * 2;
    unsigned failed = cmd == faultPhase ? (failMask >> shift) & 3 : 0;
    unsigned timeout = cmd == faultPhase ? (timeoutMask >> shift) & 3 : 0;
    control.channels[ch].trained = 3 & ~failed & ~timeout;
    if (failed) {
        unsigned rank = (failed & 1) ? 0 : 1;
        control.log(md::ERR_TRAIN, md::SOURCE_MGMT,
                    registerBase() + md::CHANNEL_BASE +
                        ch * md::CHANNEL_STRIDE + md::CH_COMMAND,
                    control.channels[ch].sequence,
                    ch | (rank << 8) | (cmd << 16));
        control.complete(ch, md::STATE_FAILED, md::ERR_TRAIN | (rank << 8));
    } else if (!timeout) {
        if (cmd == md::CMD_TRAIN) {
            controllers[ch]->startMedia();
        }
        control.complete(ch, cmd == md::CMD_TRAIN ? md::STATE_TEST
                                                  : md::STATE_CALIBRATED);
    }
    wake();
}

/** CRC 更新 NoC、Memory Tile 复位和 PLL 状态 */
void
Gw100MemoryTile::setControl(bool released, bool locked)
{
    releaseRequested = released;
    control.pllLocked = locked;
    control.released = released && !resetting[0] && !resetting[1];
    wake();
}

/** 断言内存复位，收尾旧代次 */
void
Gw100MemoryTile::resetMemory()
{
    control.reset();
    releaseRequested = false;
    for (unsigned ch = 0; ch < 2; ++ch) {
        if (training[ch] && training[ch]->scheduled()) {
            deschedule(*training[ch]);
        }
        pendingCommand[ch] = 0;
        resetting[ch] = true;
    }
    wake();
}

/** 处理一个通道的排空或恢复 */
void
Gw100MemoryTile::maintenance(unsigned ch)
{
    if (restoring[ch] || !controllers[ch]->maintenanceIdle() ||
        !links[ch]->idle() || !waiting[ch].empty() || !media[ch]->idle()) {
        return;
    }
    auto *ctrl = controllers[ch];
    for (auto *ingress : ingresses) {
        if (ingress->dataPending(control.tile * 2 + ch)) {
            return;
        }
    }
    if (resetting[ch]) {
        restoring[ch] = true;
        // 先排空旧代次，随后清除媒体控制状态。
        ctrl->quiesceMedia([this, ch, ctrl] {
            ctrl->stopMedia();
            resetting[ch] = restoring[ch] = false;
            wake();
        });
        return;
    }
    unsigned cmd = pendingCommand[ch];
    if (!cmd) {
        return;
    }
    pendingCommand[ch] = 0;
    uint64_t epoch = control.epoch;
    auto done = [this, ch, cmd, epoch] {
        if (epoch == control.epoch) {
            unsigned state = md::STATE_QUIESCED;
            if (cmd == md::CMD_SELF_REFRESH) {
                state = md::STATE_SELF_REFRESH;
            } else if (cmd == md::CMD_RESUME) {
                state = control.channels[ch].runState;
            }
            control.complete(ch, state);
        }
        wake();
    };
    if (cmd == md::CMD_QUIESCE) {
        ctrl->quiesceMedia(done);
    } else if (cmd == md::CMD_SELF_REFRESH) {
        ctrl->enterSelfRefresh(done);
    } else {
        ctrl->resumeMedia(done);
    }
}

/** 在 DDR 接收时钟端推进一个通道的 burst 队列 */
void
Gw100MemoryTile::processChannel(unsigned ch)
{
    auto &queue = waiting[ch];
    for (auto it = queue.begin(); it != queue.end();) {
        auto txn = *it;
        if (!valid(txn)) {
            it = queue.erase(it);
            finish(txn, false);
        } else if (!lockedBursts[ch].count(txn->burst)) {
            lockedBursts[ch].insert(txn->burst);
            it = queue.erase(it);
            issue(txn, txn->rmw);
        } else {
            ++it;
        }
    }
}

/** 处理事件、RMW 与生命周期 */
void
Gw100MemoryTile::process()
{
    control.released = releaseRequested && !resetting[0] && !resetting[1];
    while (!controls.empty() && controls.front().ready <= curTick()) {
        auto *pkt = controls.front().packet;
        controls.pop_front();
        accessControl(pkt);
    }
    if (!controls.empty()) {
        wake(controls.front().ready);
    }
    for (unsigned ch = 0; ch < 2; ++ch) {
        maintenance(ch);
    }
    while (!replyBlocked && !replies.empty()) {
        if (replies.front().ready > curTick()) {
            wake(replies.front().ready);
            break;
        }
        PacketPtr pkt = replies.front().packet;
        if (!sendResponse(pkt)) {
            break;
        }
        replies.pop_front();
    }
    if (requestRetry && replies.size() + controls.size() < md::FRONT_SLOTS) {
        requestRetry = false;
        front.sendRetryReq();
    }
    control.counters[4] = links[0]->cdcTicks + links[1]->cdcTicks;
    control.counters[5] = links[0]->queueTicks + links[1]->queueTicks;
    control.counters[6] = links[0]->transferTicks + links[1]->transferTicks;
    checkDrain();
}

/** 通过共享前端端口发送响应，保持统一的背压状态 */
bool
Gw100MemoryTile::sendResponse(PacketPtr pkt)
{
    if (replyBlocked) {
        return false;
    }
    if (front.sendTimingResp(pkt)) {
        return true;
    }
    replyBlocked = true;
    return false;
}

/** 检查可否结束全局排空，notify 控制异步完成通知 */
void
Gw100MemoryTile::checkDrain(bool notify)
{
    if (!draining) {
        return;
    }
    for (auto *ingress : ingresses) {
        if (!ingress->idle()) {
            return;
        }
    }
    if (!replies.empty() || !controls.empty()) {
        return;
    }
    for (unsigned ch = 0; ch < 2; ++ch) {
        if (!links[ch]->idle() || !waiting[ch].empty() || !media[ch]->idle() ||
            !controllers[ch]->maintenanceIdle()) {
            return;
        }
        unsigned s = control.channels[ch].state;
        if (!drainHeld[ch] && (s == md::STATE_TEST || s == md::STATE_READY)) {
            drainHeld[ch] = true;
            controllers[ch]->quiesceMedia([this] { wake(); });
            return;
        }
        if (control.busy(ch)) {
            return;
        }
    }
    drainComplete = true;
    if (notify && drainState() == DrainState::Draining) {
        signalDrainDone();
    }
}

/** 排空全部入口和本地事务，然后保持媒体 */
DrainState
Gw100MemoryTile::drain()
{
    draining = true;
    drainComplete = false;
    checkDrain(false);
    if (drainComplete) {
        return DrainState::Drained;
    }
    wake();
    return DrainState::Draining;
}

/** 恢复被 checkpoint 排空的媒体 */
void
Gw100MemoryTile::drainResume()
{
    draining = false;
    drainComplete = false;
    for (unsigned ch = 0; ch < 2; ++ch) {
        if (drainHeld[ch]) {
            controllers[ch]->resumeMedia([this, ch] {
                drainHeld[ch] = false;
                wake();
            });
        }
    }
}

/** 保存已排空的寄存器和状态 */
void
Gw100MemoryTile::serialize(CheckpointOut &cp) const
{
    paramOut(cp, "epoch", control.epoch);
    paramOut(cp, "released", control.released);
    paramOut(cp, "pllLocked", control.pllLocked);
    paramOut(cp, "locked", control.locked);
    paramOut(cp, "frozen", control.frozen);
    paramOut(cp, "errors", control.errors);
    paramOut(cp, "errorSource", control.errorSource);
    paramOut(cp, "errorAddr", control.errorAddr);
    paramOut(cp, "errorTxn", control.errorTxn);
    paramOut(cp, "errorInfo", control.errorInfo);
    paramOut(cp, "corruptSelftest", corruptSelftest);
    paramOut(cp, "selftestMask", selftestMask);
    arrayParamOut(cp, "counters", control.counters.data(),
                  control.counters.size());
    arrayParamOut(cp, "snapshot", control.snapshot.data(),
                  control.snapshot.size());
    for (unsigned i = 0; i < 2; ++i) {
        ScopedCheckpointSection section(cp, "channel" + std::to_string(i));
        const auto &c = control.channels[i];
        paramOut(cp, "profile", c.profile);
        paramOut(cp, "state", c.state);
        paramOut(cp, "trained", c.trained);
        paramOut(cp, "error", c.error);
        paramOut(cp, "sequence", c.sequence);
        paramOut(cp, "runState", c.runState);
        paramOut(cp, "cdcTicks", links[i]->cdcTicks);
        paramOut(cp, "queueTicks", links[i]->queueTicks);
        paramOut(cp, "transferTicks", links[i]->transferTicks);
    }
    for (unsigned i = 0; i < 4; ++i) {
        ScopedCheckpointSection section(cp, "region" + std::to_string(i));
        const auto &r = control.regions[i];
        paramOut(cp, "base", r.base);
        paramOut(cp, "limit", r.limit);
        paramOut(cp, "enabled", r.enabled);
    }
}

/** 恢复控制状态，媒体由 startup 重建 */
void
Gw100MemoryTile::unserialize(CheckpointIn &cp)
{
    restored = true;
    paramIn(cp, "epoch", control.epoch);
    paramIn(cp, "released", control.released);
    releaseRequested = control.released;
    paramIn(cp, "pllLocked", control.pllLocked);
    paramIn(cp, "locked", control.locked);
    paramIn(cp, "frozen", control.frozen);
    paramIn(cp, "errors", control.errors);
    paramIn(cp, "errorSource", control.errorSource);
    paramIn(cp, "errorAddr", control.errorAddr);
    paramIn(cp, "errorTxn", control.errorTxn);
    paramIn(cp, "errorInfo", control.errorInfo);
    paramIn(cp, "corruptSelftest", corruptSelftest);
    optParamIn(cp, "selftestMask", selftestMask, false);
    arrayParamIn(cp, "counters", control.counters.data(),
                 control.counters.size());
    arrayParamIn(cp, "snapshot", control.snapshot.data(),
                 control.snapshot.size());
    for (unsigned i = 0; i < 2; ++i) {
        ScopedCheckpointSection section(cp, "channel" + std::to_string(i));
        auto &c = control.channels[i];
        paramIn(cp, "profile", c.profile);
        paramIn(cp, "state", c.state);
        paramIn(cp, "trained", c.trained);
        paramIn(cp, "error", c.error);
        paramIn(cp, "sequence", c.sequence);
        paramIn(cp, "runState", c.runState);
        paramIn(cp, "cdcTicks", links[i]->cdcTicks);
        paramIn(cp, "queueTicks", links[i]->queueTicks);
        paramIn(cp, "transferTicks", links[i]->transferTicks);
    }
    for (unsigned i = 0; i < 4; ++i) {
        ScopedCheckpointSection section(cp, "region" + std::to_string(i));
        auto &r = control.regions[i];
        paramIn(cp, "base", r.base);
        paramIn(cp, "limit", r.limit);
        paramIn(cp, "enabled", r.enabled);
    }
}

/** 恢复 checkpoint 的媒体状态 */
void
Gw100MemoryTile::startup()
{
    if (!restored) {
        return;
    }
    for (unsigned ch = 0; ch < 2; ++ch) {
        unsigned state = control.channels[ch].state;
        if (control.channels[ch].trained != 3 ||
            (state != md::STATE_TEST && state != md::STATE_READY &&
             state != md::STATE_QUIESCED && state != md::STATE_SELF_REFRESH)) {
            continue;
        }
        controllers[ch]->startMedia();
        if (state == md::STATE_SELF_REFRESH || state == md::STATE_QUIESCED) {
            restoring[ch] = true;
            controllers[ch]->quiesceMedia([this, ch, state] {
                if (state == md::STATE_SELF_REFRESH) {
                    controllers[ch]->enterSelfRefresh([this, ch] {
                        restoring[ch] = false;
                        wake();
                    });
                } else {
                    restoring[ch] = false;
                    wake();
                }
            });
        }
    }
}

} // namespace gem5::gw100
