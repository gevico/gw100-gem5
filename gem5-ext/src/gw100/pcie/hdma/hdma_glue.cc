/**
 * @file  hdma_glue.cc
 * @brief HdmaClock、HostDmaPort、MlHdmaClient 的实现
 *
 * @see spec-host-dma, spec-pcie, spec-mgmt-link
 */

#include "src/gw100/pcie/hdma/hdma_glue.hh"

#include <cstring>

#include "mem/packet_access.hh"
#include "mem/request.hh"

namespace gem5
{
namespace gw100
{

/** 构造：owner 是所属设备，tick 在请求的时钟沿调用 */
HdmaClock::HdmaClock(ClockedObject &owner, std::function<void()> tick)
    : owner(owner), event(tick, owner.name() + ".hdma")
{
}

/** 在不早于 when 的第一个时钟沿调用 tick */
void
HdmaClock::wakeAt(Tick when)
{
    Tick e = owner.clockEdge();
    if (when > e)
        e = owner.clockEdge(owner.ticksToCycles(when - e));
    if (!event.scheduled())
        owner.schedule(event, e);
    else if (event.when() > e)
        owner.reschedule(event, e);
}

/**
 * 构造：id 为请求者 ID，line 为内存包的最大字节数（cache 行），
 * tx_credits 为在途写请求的上限
 */
HostDmaPort::HostDmaPort(const std::string &name, RequestorID id,
                         unsigned line, unsigned tx_credits)
    : RequestPort(name), requestorId(id), line(line), txCredits(tx_credits)
{
}

/** 把 [addr, addr + len) 拆成内存包排队发出 */
void
HostDmaPort::split(const std::shared_ptr<Op> &op, MemCmd cmd, Addr addr,
                   unsigned len, const uint8_t *data)
{
    Addr pa = toPhys ? toPhys(addr) : addr;
    unsigned off = 0;
    while (off < len) {
        unsigned n = std::min<unsigned>(len - off,
                                        line - (pa + off) % line);
        auto req = std::make_shared<Request>(pa + off, n, 0, requestorId);
        PacketPtr pkt = new Packet(req, cmd);
        pkt->allocate();
        if (data)
            pkt->setData(data + off);
        auto *st = new State;
        st->op = op;
        st->off = off;
        pkt->pushSenderState(st);
        sendQ.push_back(pkt);
        op->remaining++;
        off += n;
    }
    trySend();
}

/** 发出一个 MRd */
void
HostDmaPort::read(Addr addr, unsigned len, HostReadDone done)
{
    auto op = std::make_shared<Op>();
    op->buf.assign(len, 0);
    op->done = done;
    split(op, MemCmd::ReadReq, addr, len, nullptr);
}

/** 发出一个 MWr */
void
HostDmaPort::write(Addr addr, unsigned len, const uint8_t *data)
{
    auto op = std::make_shared<Op>();
    op->write = true;
    txInFlight++;
    split(op, MemCmd::WriteReq, addr, len, data);
}

/** 按顺序发出排队的内存包，直到被反压 */
void
HostDmaPort::trySend()
{
    while (!waitRetry && !sendQ.empty()) {
        if (!sendTimingReq(sendQ.front())) {
            waitRetry = true;
            return;
        }
        sendQ.pop_front();
    }
}

/** 内存系统可以再接受请求 */
void
HostDmaPort::recvReqRetry()
{
    waitRetry = false;
    trySend();
}

/** 内存包的响应 */
bool
HostDmaPort::recvTimingResp(PacketPtr pkt)
{
    auto *st = dynamic_cast<State *>(pkt->popSenderState());
    panic_if(!st, "%s: 响应没有发送状态", name());
    std::shared_ptr<Op> op = st->op;
    if (pkt->isError())
        op->err = true;
    else if (!op->write)
        pkt->writeData(op->buf.data() + st->off);
    delete st;
    delete pkt;
    if (--op->remaining > 0)
        return true;
    if (op->write) {
        txInFlight--;
        if (onTxFree)
            onTxFree();
    } else if (op->done) {
        op->done(op->err ? HostStatus::Ur : HostStatus::Ok, op->buf.data());
    }
    return true;
}

/** 接入 ML 的 HIF 侧适配器，登记一个客户端 */
void
MlHdmaClient::bind(MlEndpoint *ep)
{
    ml = ep;
    client = ml->addClient();
}

/** 读 32 B 的描述符 */
bool
MlHdmaClient::descRead(Addr addr, DescReadDone done)
{
    if (!ml)
        return false;
    auto t = std::make_shared<MlTxn>();
    t->type = MlType::DescRd;
    t->addr = addr;
    t->len = 32;
    t->done = done;
    return ml->submit(client, t);
}

/** non-posted 回写 4 B 的 DW7 */
bool
MlHdmaClient::descWrite(Addr addr, uint32_t dw7, DescWriteDone done)
{
    if (!ml)
        return false;
    auto t = std::make_shared<MlTxn>();
    t->type = MlType::DescWr;
    t->addr = addr + 28;
    t->len = 4;
    t->be = 0xF;
    std::memcpy(t->data.data(), &dw7, 4);
    t->done = [done](MlStatus s, const MlData &) { done(s); };
    return ml->submit(client, t);
}

/** posted 的 EVENT */
bool
MlHdmaClient::event(uint32_t bits)
{
    if (!ml)
        return false;
    auto t = std::make_shared<MlTxn>();
    t->type = MlType::Event;
    t->len = 4;
    t->be = 0xF;
    std::memcpy(t->data.data(), &bits, 4);
    return ml->submit(client, t);
}

/** 撤销 HDMA 已被 ML 接受的全部请求 */
void
MlHdmaClient::withdraw()
{
    if (ml)
        ml->withdraw(client);
}

} // namespace gw100
} // namespace gem5
