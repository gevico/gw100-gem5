/**
 * @file  devmem_backend.cc
 * @brief DevmemBackend 的实现
 *
 * @see spec-pcie, spec-address-map
 */

#include "src/gw100/pcie/bar2/devmem_backend.hh"

#include <memory>

#include "mem/packet.hh"

namespace gem5
{
namespace gw100
{

/**
 * 读或写 DPA 起的 size 字节：写时把 data 写入设备内存，读时把读到
 * 的数据放入 data。功能访问，返回 0，延迟由调用者另计
 */
Tick
DevmemBackend::access(Addr dpa, unsigned size, bool write, uint8_t *data)
{
    RequestPtr req = std::make_shared<Request>(dpa, size, 0, requestorId);
    Packet pkt(req, write ? MemCmd::WriteReq : MemCmd::ReadReq);
    pkt.dataStatic(data);
    memPort.sendFunctional(&pkt);
    return 0;
}

/**
 * 构造；port_name 是端口的完整名字，id 是访问设备内存的请求者 ID，
 * max_outstanding 是 timing 请求的在途上限
 */
DevmemBackend::DevmemBackend(const std::string &port_name, RequestorID id,
                             unsigned max_outstanding)
    : memPort(port_name, *this), requestorId(id),
      maxOutstanding(max_outstanding)
{
}

/** 能否再接受一个 timing 请求 */
bool
DevmemBackend::canSend() const
{
    return !blocked && outstanding < maxOutstanding;
}

/** timing 读 DPA 起的 len 字节 */
void
DevmemBackend::read(Addr dpa, unsigned len, DevReadDone done)
{
    auto req = std::make_shared<Request>(dpa, len, 0, requestorId);
    PacketPtr pkt = new Packet(req, MemCmd::ReadReq);
    pkt->allocate();
    auto *st = new State;
    st->rd = done;
    pkt->pushSenderState(st);
    send(pkt);
}

/** timing 写 DPA 起的 len 字节 */
void
DevmemBackend::write(Addr dpa, unsigned len, const uint8_t *data,
                     DevWriteDone done)
{
    writeMasked(dpa, len, data, {}, std::move(done));
}

/** 按逐字节使能发出 timing 写，mask 长度等于 len。 */
void
DevmemBackend::writeMasked(Addr dpa, unsigned len, const uint8_t *data,
                           const std::vector<bool> &mask, DevWriteDone done)
{
    auto req = std::make_shared<Request>(dpa, len, 0, requestorId);
    PacketPtr pkt = new Packet(req, MemCmd::WriteReq);
    pkt->allocate();
    pkt->setData(data);
    if (!mask.empty()) {
        req->setByteEnable(mask);
    }
    auto *st = new State;
    st->wr = done;
    pkt->pushSenderState(st);
    send(pkt);
}

/** 发出一个 timing 请求；被反压时留待重试 */
void
DevmemBackend::send(PacketPtr pkt)
{
    panic_if(!canSend(), "%s: 设备内存的端口已满", memPort.name());
    outstanding++;
    if (!memPort.sendTimingReq(pkt))
        blocked = pkt;
}

/** 设备内存可以再接受请求 */
void
DevmemBackend::retry()
{
    if (blocked && memPort.sendTimingReq(blocked))
        blocked = nullptr;
    if (!blocked && onReady)
        onReady();
}

/** 收到 timing 请求的响应 */
void
DevmemBackend::recvResp(PacketPtr pkt)
{
    auto *st = dynamic_cast<State *>(pkt->popSenderState());
    panic_if(!st, "%s: 响应没有发送状态", memPort.name());
    bool ok = !pkt->isError();
    bool was_full = outstanding >= maxOutstanding;
    outstanding--;
    if (st->rd)
        st->rd(ok, pkt->getConstPtr<uint8_t>());
    else if (st->wr)
        st->wr(ok);
    delete st;
    delete pkt;
    if (was_full && !blocked && onReady)
        onReady();
}

/** timing 请求的响应 */
bool
DevmemBackend::MemPort::recvTimingResp(PacketPtr pkt)
{
    owner.recvResp(pkt);
    return true;
}

/** 设备内存可以再接受请求 */
void
DevmemBackend::MemPort::recvReqRetry()
{
    owner.retry();
}

} // namespace gw100
} // namespace gem5
