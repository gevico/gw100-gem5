/**
 * @file  memory_ingress.cc
 * @brief Gw100MemoryIngress 的实现
 *
 * @see spec-dram, spec-address-map
 */

#include "src/gw100/mem_tile/memory_ingress.hh"

#include <algorithm>
#include <cstring>
#include "base/trace.hh"
#include "debug/Gw100Memory.hh"

#include "sim/system.hh"
#include "src/gw100/mem_tile/memory_tile.hh"

namespace gem5::gw100
{

namespace md = memorydefs;

/** 绑定所属入口 */
Gw100MemoryIngress::FrontPort::FrontPort(const std::string &name,
                                         Gw100MemoryIngress &owner)
    : ResponsePort(name), owner(owner)
{}

/** 当前实现的数据和控制范围 */
AddrRangeList
Gw100MemoryIngress::FrontPort::getAddrRanges() const
{
    AddrRangeList ranges{AddrRange(0, md::TOTAL_BYTES)};
    for (auto *tile : owner.tiles) {
        ranges.emplace_back(tile->registerBase(),
                            tile->registerBase() + md::REGISTER_BYTES);
    }
    return ranges;
}

/** 接受或反压请求 */
bool
Gw100MemoryIngress::FrontPort::recvTimingReq(PacketPtr pkt)
{ return owner.receive(pkt); }

/** 精确配置拒绝 atomic 路径 */
Tick
Gw100MemoryIngress::FrontPort::recvAtomic(PacketPtr pkt)
{ panic("%s: precise device memory requires timing requests", name()); }

/** 调试只读控制寄存器 */
void
Gw100MemoryIngress::FrontPort::recvFunctional(PacketPtr pkt)
{
    for (auto *tile : owner.tiles) {
        if (pkt->isRead() && (pkt->getSize() == 4 || pkt->getSize() == 8) &&
            pkt->getAddr() >= tile->registerBase() &&
            pkt->getAddr() < tile->registerBase() + md::REGISTER_BYTES) {
            pkt->setUintX(
                tile->readRegister(pkt->getAddr() - tile->registerBase()),
                ByteOrder::little);
            pkt->makeResponse();
            return;
        }
    }
    pkt->makeResponse();
    pkt->setBadAddress();
}

/** 上游撤销响应反压 */
void
Gw100MemoryIngress::FrontPort::recvRespRetry()
{ owner.link.retryResponse(); }

/** 绑定所属入口 */
Gw100MemoryIngress::BackPort::BackPort(const std::string &name,
                                       Gw100MemoryIngress &owner)
    : RequestPort(name), owner(owner)
{}

/** 排队并尝试发送一个有界子请求 */
void
Gw100MemoryIngress::BackPort::send(PacketPtr pkt)
{
    if (waiting.empty() && sendTimingReq(pkt)) {
        return;
    }
    waiting.push_back(pkt);
}

/** 接受返回包 */
bool
Gw100MemoryIngress::BackPort::recvTimingResp(PacketPtr pkt)
{
    owner.returned(pkt);
    return true;
}

/** 下游撤销请求反压 */
void
Gw100MemoryIngress::BackPort::recvReqRetry()
{
    while (!waiting.empty() && sendTimingReq(waiting.front())) {
        waiting.pop_front();
    }
    owner.progress();
}

/** 绑定时钟、设备 System 和来源 */
Gw100MemoryIngress::Gw100MemoryIngress(const Params &p)
    : ClockedObject(p),
      source(p.source_id),
      requestor(p.system->getRequestorId(this)),
      tiles(p.tiles),
      front(name() + ".cpu_side", *this),
      back(name() + ".mem_side", *this),
      link(*this, *p.source_domain, *p.clk_domain, name(), md::SOURCE_LIMIT),
      returnEvent([this] { processReturns(); }, name() + ".returns")
{
    fatal_if(source > md::SOURCE_BAR2 || tiles.empty(),
             "%s: invalid memory ingress configuration", name());
    for (auto *tile : tiles) {
        tile->attachIngress(this);
    }
    link.onProgress = [this] { progress(); };
}

/** 查找上下游端口 */
Port &
Gw100MemoryIngress::getPort(const std::string &name, PortID idx)
{
    if (name == "cpu_side") {
        return front;
    }
    if (name == "mem_side") {
        return back;
    }
    return ClockedObject::getPort(name, idx);
}

/** 发布地址范围 */
void
Gw100MemoryIngress::init()
{
    fatal_if(!front.isConnected() || !back.isConnected(),
             "%s: memory ingress is not connected", name());
    front.sendRangeChange();
}

/** CRC 释放或复位连接路径 */
void
Gw100MemoryIngress::setEnabled(bool value)
{
    if (enabled && !value) {
        invalidate();
    }
    enabled = value;
}

/** FLR 或电源切换作废来源旧事务 */
void
Gw100MemoryIngress::invalidate()
{ ++generation; }

/** 来源事务和同步信用均已排空 */
bool
Gw100MemoryIngress::idle()
{ return link.idle() && back.idle() && returns.empty(); }

/** 上游已停止发请求后排空链路 */
DrainState
Gw100MemoryIngress::drain()
{ return idle() ? DrainState::Drained : DrainState::Draining; }

/** 保存空链路的来源代次和计数器 */
void
Gw100MemoryIngress::serialize(CheckpointOut &cp) const
{
    SERIALIZE_SCALAR(enabled);
    SERIALIZE_SCALAR(generation);
    SERIALIZE_SCALAR(nextTransaction);
    arrayParamOut(cp, "counters", counters.data(), counters.size());
    paramOut(cp, "cdcTicks", link.cdcTicks);
    paramOut(cp, "queueTicks", link.queueTicks);
    paramOut(cp, "transferTicks", link.transferTicks);
}

/** 指定全局通道仍有已准入的数据事务 */
bool
Gw100MemoryIngress::dataPending(unsigned channel) const
{ return pendingData.at(channel) != 0; }

/** 恢复空链路的来源代次和计数器 */
void
Gw100MemoryIngress::unserialize(CheckpointIn &cp)
{
    UNSERIALIZE_SCALAR(enabled);
    UNSERIALIZE_SCALAR(generation);
    UNSERIALIZE_SCALAR(nextTransaction);
    arrayParamIn(cp, "counters", counters.data(), counters.size());
    paramIn(cp, "cdcTicks", link.cdcTicks);
    paramIn(cp, "queueTicks", link.queueTicks);
    paramIn(cp, "transferTicks", link.transferTicks);
}

/** 读取请求、字节、retry、CDC、排队、传输的累计值 */
uint64_t
Gw100MemoryIngress::metric(unsigned index) const
{
    if (index < 4) {
        return counters[index];
    }
    if (index == 4) {
        return link.cdcTicks;
    }
    if (index == 5) {
        return link.queueTicks;
    }
    return index == 6 ? link.transferTicks : 0;
}

/** 接受一个完整请求 */
bool
Gw100MemoryIngress::receive(PacketPtr pkt)
{
    DPRINTF(Gw100Memory, "accept %s %#x %u\n", pkt->cmdString(),
            pkt->getAddr(), pkt->getSize());
    if (!link.canSubmit()) {
        requestRetry = true;
        ++counters[3];
        return false;
    }
    auto ctx = std::make_shared<Context>();
    ctx->item = std::make_shared<MemoryLink::Item>();
    ctx->item->packet = pkt;
    ctx->generation = generation;
    ctx->transaction = nextTransaction++;
    ctx->item->availableAt = curTick() + pkt->headerDelay + pkt->payloadDelay;
    pkt->headerDelay = pkt->payloadDelay = 0;
    ctx->item->receive = [this, ctx] {
        auto *pkt = ctx->item->packet;
        ctx->error =
            !enabled ||
            (pkt->cmd != MemCmd::ReadReq && pkt->cmd != MemCmd::WriteReq) ||
            !pkt->getSize() || pkt->getSize() > md::INTERLEAVE_BYTES;
        Addr addr = pkt->getAddr();
        if (!ctx->error && addr < md::TOTAL_BYTES) {
            for (auto *tile : tiles) {
                unsigned error =
                    tile->checkAccess(addr, pkt->getSize(), source);
                if (error) {
                    ctx->error = true;
                    if (ctx->generation == generation) {
                        tile->recordError(error, source, addr,
                                          ctx->transaction);
                    }
                }
            }
        } else if (!ctx->error) {
            bool found = false;
            for (auto *tile : tiles) {
                found |= addr >= tile->registerBase() &&
                         addr - tile->registerBase() < md::REGISTER_BYTES &&
                         pkt->getSize() <=
                             tile->registerBase() + md::REGISTER_BYTES - addr;
            }
            ctx->error = !found || source != md::SOURCE_MGMT;
        }
        ++counters[0];
        counters[pkt->isWrite() ? 2 : 1] += pkt->getSize();
        if (ctx->error) {
            finish(ctx);
        } else {
            if (addr < md::TOTAL_BYTES) {
                for (Addr a = addr; a < addr + pkt->getSize();) {
                    ctx->channels |= 1u << ((a >> 8) & 7);
                    a = std::min(addr + pkt->getSize(), (a | 255) + 1);
                }
                for (unsigned ch = 0; ch < md::CHANNELS; ++ch) {
                    if (ctx->channels & (1u << ch)) {
                        ++pendingData[ch];
                    }
                }
            }
            forward(ctx);
        }
    };
    ctx->item->complete = [this, ctx] {
        if (!front.sendTimingResp(ctx->item->packet)) {
            return false;
        }
        for (unsigned ch = 0; ch < md::CHANNELS; ++ch) {
            if (ctx->channels & (1u << ch)) {
                --pendingData[ch];
            }
        }
        return true;
    };
    link.submit(ctx->item);
    return true;
}

/** 完整请求预检后开始发送 */
void
Gw100MemoryIngress::forward(const std::shared_ptr<Context> &ctx)
{
    DPRINTF(Gw100Memory, "forward %#x offset %u\n",
            ctx->item->packet->getAddr(), ctx->offset);
    if (ctx->generation != generation) {
        ctx->error = true;
        finish(ctx);
        return;
    }
    PacketPtr orig = ctx->item->packet;
    Addr addr = orig->getAddr() + ctx->offset;
    unsigned size = orig->getSize() - ctx->offset;
    if (addr < md::TOTAL_BYTES) {
        size = std::min(size, unsigned(md::BURST_BYTES - addr % 64));
    }
    auto req = std::make_shared<Request>(addr, size, 0, requestor);
    const auto &byte_enable = orig->req->getByteEnable();
    auto *pkt =
        new Packet(req, orig->isWrite() ? MemCmd::WriteReq : MemCmd::ReadReq);
    pkt->allocate();
    if (orig->isWrite()) {
        pkt->setData(orig->getConstPtr<uint8_t>() + ctx->offset);
    }
    req->setByteEnable(std::vector<bool>(byte_enable.begin() + ctx->offset,
                                         byte_enable.begin() + ctx->offset + size));
    auto *st = new ForwardState;
    st->source = source;
    st->transaction = ctx->transaction;
    st->context = ctx;
    st->admitted = ctx->channels != 0;
    st->size = size;
    st->valid = [this, g = generation] { return g == generation; };
    if (addr < md::TOTAL_BYTES) {
        unsigned tile = (addr >> 9) & 3;
        for (auto *t : tiles) {
            if ((t->registerBase() - md::TILE_WINDOW_BASE -
                 md::REGISTER_OFFSET) /
                    md::TILE_WINDOW_STRIDE ==
                tile) {
                st->epoch = t->epoch();
            }
        }
    }
    pkt->pushSenderState(st);
    back.send(pkt);
}

/** 返回一个子请求 */
void
Gw100MemoryIngress::returned(PacketPtr pkt)
{
    Tick ready = curTick() + pkt->headerDelay + pkt->payloadDelay;
    pkt->headerDelay = pkt->payloadDelay = 0;
    auto pos =
        std::upper_bound(returns.begin(), returns.end(), ready,
                         [](Tick t, const auto &r) { return t < r.first; });
    returns.insert(pos, {ready, pkt});
    if (!returnEvent.scheduled()) {
        schedule(returnEvent, ready);
    } else if (ready < returnEvent.when()) {
        reschedule(returnEvent, ready);
    }
}

/** 推进有界的互连返回队列 */
void
Gw100MemoryIngress::processReturns()
{
    while (!returns.empty() && returns.front().first <= curTick()) {
        PacketPtr pkt = returns.front().second;
        returns.pop_front();
        completeReturn(pkt);
    }
    if (!returns.empty()) {
        schedule(returnEvent, returns.front().first);
    }
    progress();
}

/** 在互连返回延迟结束后处理子请求 */
void
Gw100MemoryIngress::completeReturn(PacketPtr pkt)
{
    DPRINTF(Gw100Memory, "returned %s %#x\n", pkt->cmdString(),
            pkt->getAddr());
    auto *st = dynamic_cast<ForwardState *>(pkt->popSenderState());
    panic_if(!st, "%s: missing memory ingress state", name());
    auto ctx = st->context;
    bool ok = !pkt->isError() && ctx->generation == generation;
    if (ok && pkt->isRead()) {
        std::memcpy(ctx->item->packet->getPtr<uint8_t>() + ctx->offset,
                    pkt->getConstPtr<uint8_t>(), st->size);
    }
    ctx->offset += st->size;
    ctx->error |= !ok;
    delete st;
    delete pkt;
    if (ctx->error || ctx->offset == ctx->item->packet->getSize()) {
        finish(ctx);
    } else {
        forward(ctx);
    }
}

/** 完成或拒绝一个原始请求 */
void
Gw100MemoryIngress::finish(const std::shared_ptr<Context> &ctx)
{
    PacketPtr pkt = ctx->item->packet;
    if (ctx->error && pkt->isRead()) {
        std::memset(pkt->getPtr<uint8_t>(), 0xFF, pkt->getSize());
    }
    pkt->makeResponse();
    if (ctx->error) {
        pkt->setBadCommand();
    }
    link.respond(ctx->item);
}

/** 通知 retry、排空和关联 Tile */
void
Gw100MemoryIngress::progress()
{
    if (requestRetry && link.canSubmit()) {
        requestRetry = false;
        front.sendRetryReq();
    }
    if (drainState() == DrainState::Draining && idle()) {
        signalDrainDone();
    }
    for (auto *tile : tiles) {
        tile->wake();
    }
}

} // namespace gem5::gw100
