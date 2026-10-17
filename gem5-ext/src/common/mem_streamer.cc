/**
 * @file  mem_streamer.cc
 * @brief UnitPort 与 MemStreamer 的实现
 */

#include "src/common/mem_streamer.hh"

#include <algorithm>

#include "base/cast.hh"
#include "base/logging.hh"
#include "mem/request.hh"

namespace gem5
{
namespace acis
{

// ---------------------------------------------------------------- UnitPort

UnitPort::UnitPort(const std::string &name, ClockedObject &unit_obj)
    : RequestPort(name), unit(unit_obj)
{
}

void
UnitPort::send(PacketPtr pkt)
{
    pending.push_back(pkt);
    trySend();
}

void
UnitPort::trySend()
{
    while (!blocked && !pending.empty()) {
        if (sendTimingReq(pending.front()))
            pending.pop_front();
        else
            blocked = true;
    }
}

void
UnitPort::recvReqRetry()
{
    blocked = false;
    trySend();
}

bool
UnitPort::recvTimingResp(PacketPtr pkt)
{
    auto *rs = safe_cast<MemStreamer::ReqState *>(pkt->senderState);
    rs->streamer->handleResp(pkt);
    return true;
}

// ---------------------------------------------------------------- MemStreamer

MemStreamer::MemStreamer(const std::string &name, RequestorID id, unsigned req_bytes,
                         unsigned max_outstanding)
    : _name(name), requestorId(id), reqBytes(req_bytes), maxOutstanding(max_outstanding)
{
    fatal_if(req_bytes == 0 || (req_bytes & (req_bytes - 1)),
             "%s: 请求大小必须是 2 的幂，收到 %u", name, req_bytes);
}

void
MemStreamer::reset(std::vector<ByteRun> new_runs, PortSelect sel, bool is_read)
{
    panic_if(active, "%s: 上一次传输尚未结束", _name);
    runs = std::move(new_runs);
    select = std::move(sel);
    isRead = is_read;
    active = true;
    runIdx = 0;
    runOff = 0;
    issuedOff = 0;
    outstanding = 0;
    donePrefix = 0;
    doneRanges.clear();
    total = 0;
    for (const auto &r : runs)
        total += r.bytes;
}

void
MemStreamer::startRead(std::vector<ByteRun> new_runs, uint8_t *dense, PortSelect sel,
                       std::function<void()> on_progress, std::function<void()> on_done)
{
    reset(std::move(new_runs), std::move(sel), true);
    readBuf = dense;
    writeBuf = nullptr;
    limit = nullptr;
    onProgress = std::move(on_progress);
    onDone = std::move(on_done);
    issue();
    finishIfDone();
}

void
MemStreamer::startWrite(std::vector<ByteRun> new_runs, const uint8_t *dense, PortSelect sel,
                        std::function<void()> on_done, const uint64_t *lim)
{
    reset(std::move(new_runs), std::move(sel), false);
    readBuf = nullptr;
    writeBuf = dense;
    limit = lim;
    onProgress = nullptr;
    onDone = std::move(on_done);
    issue();
    finishIfDone();
}

void
MemStreamer::issue()
{
    while (active && outstanding < maxOutstanding && runIdx < runs.size()) {
        const ByteRun &run = runs[runIdx];
        const Addr addr = run.addr + runOff;
        // 不跨 reqBytes 对齐边界
        const uint64_t to_boundary = reqBytes - (addr & (reqBytes - 1));
        const uint64_t len = std::min<uint64_t>(run.bytes - runOff, to_boundary);
        if (limit && issuedOff + len > *limit)
            break;

        auto req = std::make_shared<Request>(addr, static_cast<unsigned>(len), 0, requestorId);
        PacketPtr pkt = isRead ? Packet::createRead(req) : Packet::createWrite(req);
        pkt->allocate();
        if (!isRead)
            pkt->setData(writeBuf + issuedOff);
        pkt->pushSenderState(new ReqState(this, issuedOff));

        UnitPort *port = select(addr);
        panic_if(!port, "%s: 地址 %#x 没有可用的端口", _name, addr);
        outstanding++;
        issuedOff += len;
        runOff += len;
        if (runOff == run.bytes) {
            runIdx++;
            runOff = 0;
        }
        port->send(pkt);
    }
}

void
MemStreamer::handleResp(PacketPtr pkt)
{
    auto *rs = safe_cast<ReqState *>(pkt->popSenderState());
    const uint64_t off = rs->denseOff;
    const uint64_t len = pkt->getSize();
    delete rs;

    panic_if(pkt->isError(), "%s: 访问 %#x 出错", _name, pkt->getAddr());
    if (isRead)
        pkt->writeData(readBuf + off);
    delete pkt;

    outstanding--;
    // 维护"从头开始已完成的连续字节数"
    if (off == donePrefix) {
        donePrefix += len;
        for (auto it = doneRanges.begin(); it != doneRanges.end() && it->first == donePrefix;
             it = doneRanges.erase(it)) {
            donePrefix += it->second;
        }
    } else {
        doneRanges[off] = len;
    }
    if (isRead && onProgress)
        onProgress();

    issue();
    finishIfDone();
}

void
MemStreamer::finishIfDone()
{
    if (!active || runIdx < runs.size() || outstanding > 0)
        return;
    active = false;
    if (onDone) {
        // 回调里可能马上开始下一次传输，所以先把回调移出来
        auto cb = std::move(onDone);
        onDone = nullptr;
        cb();
    }
}

} // namespace acis
} // namespace gem5
