/**
 * @file  ml_endpoint.cc
 * @brief MlEndpoint 的实现
 *
 * @see spec-mgmt-link
 */

#include "src/gw100/ml/ml_endpoint.hh"

#include <algorithm>

namespace gem5
{
namespace gw100
{

/** 构造：name 用于调试输出 */
MlEndpoint::MlEndpoint(const std::string &name, const Config &cfg)
    : name(name), cfg(cfg), tagTable(cfg.tags)
{
}

/**
 * 连接本端的时钟、目标、四个通道和对端。tx 是本端发出的方向，
 * rx 是本端接收的方向
 */
void
MlEndpoint::bind(MlClockIf *clk_if, MlTargetIf *tgt, ReqFifo *tx_req,
                 RspFifo *tx_rsp, ReqFifo *rx_req, RspFifo *rx_rsp,
                 MlEndpoint *peer_ep)
{
    clk = clk_if;
    target = tgt;
    txReq = tx_req;
    txRsp = tx_rsp;
    rxReq = rx_req;
    rxRsp = rx_rsp;
    peer = peer_ep;
}

/** 登记一个客户端，返回它的编号 */
unsigned
MlEndpoint::addClient()
{
    queues.emplace_back();
    return queues.size() - 1;
}

/** 检查客户端请求是否满足 §4.3 的长度与对齐规则 */
bool
MlEndpoint::validRequest(const MlTxn &t)
{
    if (t.len == 0 || t.len > mlMaxPayload)
        return false;
    switch (t.type) {
      case MlType::DbWr:
      case MlType::Event:
      case MlType::Barrier:
        return t.len == 4;
      case MlType::BootWr: {
        uint64_t all = t.len == 64 ? ~0ULL : ((1ULL << t.len) - 1);
        return t.addr % 8 == 0 && t.len % 8 == 0 &&
               (t.addr % 64) + t.len <= 64 && (t.be & all) == all;
      }
      case MlType::DescRd:
        return t.addr % 4 == 0 && t.len % 4 == 0 &&
               (t.addr % 64) + t.len <= 64;
      case MlType::DescWr:
        return t.addr % 4 == 0 && (t.len == 4 || t.len == 8) &&
               (t.addr % 8) + t.len <= 8;
      case MlType::RegRd:
      case MlType::RegWr:
        return (t.len == 4 || t.len == 8) && t.addr % t.len == 0;
    }
    return false;
}

/** 以 status 结束一个 non-posted 请求（已撤销的不调用回调） */
void
MlEndpoint::finish(const MlTxnPtr &t, MlStatus status, const MlData &data)
{
    if (!t->withdrawn && t->done)
        t->done(status, data);
}

/**
 * 客户端提交一个请求。返回 false 表示链路尚未建立（DOWN），适配器
 * 不接受，客户端须自行保留；故障态下立即结束（non-posted 以 FAULT
 * 结束，posted 写丢弃）并返回 true
 */
bool
MlEndpoint::submit(unsigned client, const MlTxnPtr &txn)
{
    if (!validRequest(*txn)) {
        // 客户端的实现错误：不发出，non-posted 立即以 REJECT 结束
        recordErr(ErrBadReq, txn.get());
        if (mlNonPosted(txn->type))
            finish(txn, MlStatus::Reject, MlData{});
        return true;
    }
    if (linkState == MlLinkState::Down)
        return false;
    if (linkState == MlLinkState::Failed) {
        if (mlNonPosted(txn->type))
            finish(txn, MlStatus::Fault, MlData{});
        return true;
    }
    txn->client = client;
    txn->seq = nextSeq++;
    txn->withdrawn = false;
    queues.at(client).push_back(txn);
    clk->wakeAt(clk->now());
    return true;
}

/** 客户端复位：撤销该客户端已被接受的全部请求（§7.4） */
void
MlEndpoint::withdraw(unsigned client)
{
    for (auto &t : queues.at(client))
        t->withdrawn = true;
    for (auto &e : tagTable) {
        if (e.inUse && e.txn->client == client)
            e.txn->withdrawn = true;
    }
    if (curTx && curTx->client == client)
        curTx->withdrawn = true;
}

/** 记录一个错误位；ML_ERR [7:0] 为 0 时同时记录事务信息 */
void
MlEndpoint::recordErr(unsigned bit, const MlTxn *t)
{
    bool first = (err & 0xFF) == 0;
    err |= 1u << bit;
    if (first) {
        uint32_t type = t ? static_cast<uint32_t>(t->type) & 0xF : 0;
        uint32_t tag = t ? (t->tag & 0x1F) : 0;
        errInfo = type | (tag << 4) | ((bit & 0x1F) << 9);
        errAddr = t ? static_cast<uint32_t>(t->addr) : 0;
    }
    if (onChange)
        onChange();
}

/** 结束本端全部已接受的请求：non-posted 以 status 结束 */
void
MlEndpoint::abortAll(MlStatus status)
{
    // 先摘下全部请求，再调用回调，回调中再次提交也不会被这里清掉
    std::vector<MlTxnPtr> victims;
    for (auto &q : queues) {
        for (auto &t : q)
            victims.push_back(t);
        q.clear();
    }
    for (auto &e : tagTable) {
        if (e.inUse)
            victims.push_back(e.txn);
        e = TagEntry{};
    }
    curTx = nullptr;
    curTxBeat = 0;
    stallSince = MaxTick;
    for (auto &t : victims) {
        if (mlNonPosted(t->type))
            finish(t, status, MlData{});
    }
}

/** 进入故障态（§7.5）；bit 为本地原因 */
void
MlEndpoint::fail(unsigned bit, const MlTxn *t)
{
    if (linkState == MlLinkState::Failed)
        return;
    linkState = MlLinkState::Failed;
    gen++;
    recordErr(bit, t);
    rspOut.clear();
    curRspBeat = 0;
    curRx = nullptr;
    curRxDelivered = false;
    sendSignal(1, true);
    abortAll(MlStatus::Fault);
    if (onChange)
        onChange();
}

/** 本端复位置位：清除全部状态，就绪信号变为 0 */
void
MlEndpoint::resetAssert()
{
    inReset = true;
    linkState = MlLinkState::Down;
    gen++;
    for (auto &q : queues)
        q.clear();
    for (auto &e : tagTable)
        e = TagEntry{};
    curTx = nullptr;
    curTxBeat = 0;
    stallSince = MaxTick;
    rspOut.clear();
    curRspBeat = 0;
    curRx = nullptr;
    curRxDelivered = false;
    err = errInfo = errAddr = 0;
    peerReady = peerFailed = hifErrMirror = false;
    sigQueue.clear();
    txReq->reset();
    txRsp->reset();
    rxReq->reset();
    rxRsp->reset();
    sendSignal(0, false);
    sendSignal(1, false);
    sendSignal(2, false);
    if (onChange)
        onChange();
}

/** 本端复位撤销：送出就绪信号 */
void
MlEndpoint::resetRelease()
{
    inReset = false;
    sendSignal(0, true);
    clk->wakeAt(clk->now());
}

/** 只在 HIF 侧：设置送往对端的"ML_HIF_ERR 非零"电平 */
void
MlEndpoint::setHifErrLevel(bool level)
{
    sendSignal(2, level);
}

/** 向对端送出状态信号 */
void
MlEndpoint::sendSignal(unsigned sig, bool level)
{
    if (peer)
        peer->peerSignal(sig, level, clk->now());
}

/** 对端调用：它的某个状态信号在时刻 t 变为 level */
void
MlEndpoint::peerSignal(unsigned sig, bool level, Tick t)
{
    Tick at = clk->edgeAfter(t) + (cfg.syncStages - 1) * clk->period();
    sigQueue.push_back(SigChange{at, sig, level});
    clk->wakeAt(at);
}

/** 应用已生效的对端信号，必要时改变链路状态 */
void
MlEndpoint::applySignals(Tick now)
{
    bool mirror_old = hifErrMirror;
    while (!sigQueue.empty() && sigQueue.front().at <= now) {
        const SigChange &c = sigQueue.front();
        if (c.sig == 0)
            peerReady = c.level;
        else if (c.sig == 1)
            peerFailed = c.level;
        else
            hifErrMirror = c.level;
        sigQueue.pop_front();
    }
    if (mirror_old != hifErrMirror && cfg.mgmtSide && onChange)
        onChange();

    if (linkState == MlLinkState::Up && peerFailed) {
        fail(ErrPeerFailed, nullptr);
        return;
    }
    if (linkState == MlLinkState::Up && !peerReady) {
        // §7.3：对端进入复位
        gen++;
        std::vector<MlTxnPtr> victims;
        for (auto &e : tagTable) {
            if (e.inUse) {
                recordErr(ErrPeerReset, e.txn.get());
                victims.push_back(e.txn);
            }
            e = TagEntry{};
        }
        for (auto &q : queues) {
            for (auto &t : q)
                victims.push_back(t);
            q.clear();
        }
        curTx = nullptr;
        curTxBeat = 0;
        stallSince = MaxTick;
        rspOut.clear();
        curRspBeat = 0;
        curRx = nullptr;
        curRxDelivered = false;
        linkState = MlLinkState::Down;
        for (auto &t : victims) {
            if (mlNonPosted(t->type))
                finish(t, MlStatus::Fault, MlData{});
        }
        if (onChange)
            onChange();
        return;
    }
    if (linkState == MlLinkState::Down && !inReset && peerReady &&
        !peerFailed) {
        linkState = MlLinkState::Up;
        if (onChange)
            onChange();
    }
}

/** 接收响应通道中的一拍 */
void
MlEndpoint::receiveResponse(Tick now)
{
    if (lastRxRsp == now || !rxRsp->frontVisible(now))
        return;
    auto beat = rxRsp->front();
    rxRsp->pop(now);
    lastRxRsp = now;
    if (!beat.last)
        return;
    const MlRspPtr &rsp = beat.payload;
    if (rsp->tag >= tagTable.size() || !tagTable[rsp->tag].inUse) {
        fail(ErrProtocol, nullptr);
        return;
    }
    MlTxnPtr txn = tagTable[rsp->tag].txn;
    tagTable[rsp->tag] = TagEntry{};
    if (rsp->status == MlStatus::Reject) {
        if (!txn->withdrawn)
            recordErr(ErrNpReject, txn.get());
        finish(txn, MlStatus::Reject, MlData{});
    } else {
        finish(txn, MlStatus::Ok, rsp->data);
    }
}

/** 接收请求通道中的一拍；收齐后交给目标 */
void
MlEndpoint::receiveRequest(Tick now)
{
    if (curRxDelivered || lastRxReq == now || !rxReq->frontVisible(now))
        return;
    auto beat = rxReq->front();
    rxReq->pop(now);
    lastRxReq = now;
    if (beat.idx == 0)
        curRx = beat.payload;
    if (!beat.last)
        return;
    if (!curRx || !validRequest(*curRx)) {
        fail(ErrProtocol, curRx.get());
        return;
    }
    curRxDelivered = true;
    MlTxnPtr t = curRx;
    uint64_t g = gen;
    target->mlRequest(t, [this, t, g](MlStatus st, const MlData &d) {
        onTargetDone(t, st, d, g);
    });
}

/** 目标处理完 txn 后的处理；gen 不等于当前代时忽略（已复位） */
void
MlEndpoint::onTargetDone(const MlTxnPtr &txn, MlStatus status,
                         const MlData &data, uint64_t g)
{
    if (g != gen)
        return;
    if (mlNonPosted(txn->type)) {
        auto rsp = std::make_shared<MlRsp>();
        rsp->tag = txn->tag;
        rsp->status = status == MlStatus::Ok ? MlStatus::Ok
                                             : MlStatus::Reject;
        if (rsp->status == MlStatus::Ok && mlIsRead(txn->type)) {
            rsp->data = data;
            rsp->len = txn->len;
        }
        rspOut.push_back(rsp);
    } else if (status != MlStatus::Ok) {
        recordErr(ErrWrReject, txn.get());
    }
    curRx = nullptr;
    curRxDelivered = false;
    clk->wakeAt(clk->now());
}

/** 向响应通道写一拍 */
void
MlEndpoint::sendResponse(Tick now)
{
    if (rspOut.empty() || lastTxRsp == now || !txRsp->canPush(now))
        return;
    MlRspPtr rsp = rspOut.front();
    unsigned n = rsp->beats();
    txRsp->push(rsp, curRspBeat, curRspBeat + 1 == n, now);
    lastTxRsp = now;
    if (++curRspBeat == n) {
        rspOut.pop_front();
        curRspBeat = 0;
    }
}

/**
 * 轮询各客户端，选出下一个可以发出的请求；没有时返回空。pop 为
 * false 时只判断，不取出
 */
MlTxnPtr
MlEndpoint::arbitrate(bool pop)
{
    if (queues.empty())
        return nullptr;
    uint64_t min_seq = UINT64_MAX;
    uint64_t min_barrier = UINT64_MAX;
    for (auto &q : queues) {
        if (q.empty())
            continue;
        min_seq = std::min(min_seq, q.front()->seq);
        for (auto &t : q) {
            if (t->type == MlType::Barrier)
                min_barrier = std::min(min_barrier, t->seq);
        }
    }
    if (min_seq == UINT64_MAX)
        return nullptr;
    unsigned free_tags = 0;
    for (auto &e : tagTable)
        free_tags += e.inUse ? 0 : 1;

    unsigned n = queues.size();
    for (unsigned i = 0; i < n; i++) {
        unsigned c = (rrNext + i) % n;
        if (queues[c].empty())
            continue;
        MlTxnPtr t = queues[c].front();
        if (t->type == MlType::Barrier) {
            // 此前被接受的全部请求都已发出
            if (t->seq != min_seq)
                continue;
        } else if (min_barrier < t->seq) {
            // 不得越过此前被接受、尚未发出的屏障事件
            continue;
        }
        if (mlNonPosted(t->type) && free_tags == 0)
            continue;
        if (pop) {
            queues[c].pop_front();
            rrNext = (c + 1) % n;
        }
        return t;
    }
    return nullptr;
}

/** 向请求通道写一拍；必要时先仲裁出下一个事务 */
void
MlEndpoint::sendRequest(Tick now)
{
    if (lastTxReq == now)
        return;
    if (!curTx) {
        curTx = arbitrate(true);
        if (!curTx) {
            stallSince = MaxTick;
            return;
        }
        curTxBeat = 0;
        if (mlNonPosted(curTx->type)) {
            for (unsigned i = 0; i < tagTable.size(); i++) {
                if (!tagTable[i].inUse) {
                    tagTable[i].inUse = true;
                    tagTable[i].txn = curTx;
                    tagTable[i].issued = MaxTick;
                    curTx->tag = i;
                    break;
                }
            }
        }
    }
    if (!txReq->canPush(now)) {
        if (stallSince == MaxTick)
            stallSince = now;
        else if (now - stallSince >= cfg.timeout)
            fail(ErrStall, curTx.get());
        return;
    }
    stallSince = MaxTick;
    unsigned n = curTx->reqBeats();
    if (curTxBeat == 0 && mlNonPosted(curTx->type))
        tagTable[curTx->tag].issued = now;
    txReq->push(curTx, curTxBeat, curTxBeat + 1 == n, now);
    lastTxReq = now;
    if (++curTxBeat == n) {
        curTx = nullptr;
        curTxBeat = 0;
    }
}

/** 检查 tag 超时 */
void
MlEndpoint::checkTimeouts(Tick now)
{
    for (auto &e : tagTable) {
        if (e.inUse && e.issued != MaxTick &&
            now >= e.issued + cfg.timeout) {
            fail(ErrTimeout, e.txn.get());
            return;
        }
    }
}

/** 本端在当前时钟沿完成能做的全部工作，并请求下一次唤醒 */
void
MlEndpoint::process()
{
    Tick now = clk->now();
    applySignals(now);
    if (linkState == MlLinkState::Up) {
        receiveResponse(now);
        if (linkState == MlLinkState::Up)
            receiveRequest(now);
        if (linkState == MlLinkState::Up)
            sendResponse(now);
        if (linkState == MlLinkState::Up)
            sendRequest(now);
        if (linkState == MlLinkState::Up)
            checkTimeouts(now);
    }
    scheduleNext(now);
}

/** 计算并请求下一次唤醒 */
void
MlEndpoint::scheduleNext(Tick now)
{
    Tick next = MaxTick;
    if (!sigQueue.empty())
        next = std::min(next, sigQueue.front().at);
    if (linkState == MlLinkState::Up) {
        Tick e = clk->edgeAfter(now);
        if (!rspOut.empty())
            next = std::min(next, std::max(e, txRsp->nextFreeAt(e)));
        if (curTx || arbitrate(false)) {
            next = std::min(next, std::max(e, txReq->nextFreeAt(e)));
            if (stallSince != MaxTick)
                next = std::min(next, stallSince + cfg.timeout);
        }
        if (!rxRsp->empty())
            next = std::min(next, std::max(e, rxRsp->front().visibleAt));
        if (!curRxDelivered && !rxReq->empty())
            next = std::min(next, std::max(e, rxReq->front().visibleAt));
        for (auto &t : tagTable) {
            if (t.inUse && t.issued != MaxTick)
                next = std::min(next, t.issued + cfg.timeout);
        }
    }
    if (next != MaxTick)
        clk->wakeAt(next);
}

/** ML_STATUS：[0] UP，[1] FAILED */
uint32_t
MlEndpoint::statusReg() const
{
    return (linkState == MlLinkState::Up ? 1u : 0u) |
           (linkState == MlLinkState::Failed ? 2u : 0u);
}

/** ML_ERR：[7:0] 粘滞位；管理 Tile 侧另有 [8] HIF_ERR 镜像 */
uint32_t
MlEndpoint::errReg() const
{
    uint32_t v = err;
    if (cfg.mgmtSide && hifErrMirror)
        v |= 1u << ErrHifErr;
    return v;
}

/** 对 ML_ERR 写 1 清零 [7:0]；[8] 不可写 */
void
MlEndpoint::clearErr(uint32_t mask)
{
    err &= ~(mask & 0xFF);
    if (onChange)
        onChange();
}

/** 当前在途（已发出、未结束）的 non-posted 请求数，供测试 */
unsigned
MlEndpoint::outstanding() const
{
    unsigned n = 0;
    for (auto &e : tagTable)
        n += e.inUse ? 1 : 0;
    return n;
}

} // namespace gw100
} // namespace gem5
