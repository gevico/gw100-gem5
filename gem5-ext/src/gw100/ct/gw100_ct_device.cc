/**
 * @file  gw100_ct_device.cc
 * @brief Compute Tile 的实现
 *
 * @see spec-ct
 */

#include "src/gw100/ct/gw100_ct_device.hh"

#include "base/logging.hh"
#include "src/gw100/ct/ct_types.hh"
#include "src/gw100/ct/ct_windows.hh"
#include "src/gw100/ct/ctrl/ct_ctrl_ram.hh"

namespace gem5
{
namespace gw100
{

/** clock 子函数实现 */
/** 构造：owner 是所属设备，tick 在请求的时钟沿调用 */
CtClock::CtClock(ClockedObject &owner, std::function<void()> tick)
    : owner(owner), event(tick, owner.name() + ".ct")
{
}

/** 在不早于 when 的第一个时钟沿调用 tick */
void
CtClock::wakeAt(Tick when)
{
    Tick e = owner.clockEdge();
    if (when > e)
        e = owner.clockEdge(owner.ticksToCycles(when - e));
    if (!event.scheduled())
        owner.schedule(event, e);
    else if (event.when() > e)
        owner.reschedule(event, e);
}

/** pio 端口实现 */
CtPioPort::CtPioPort(const std::string &name, ClockedObject &owner,
                     CtWindowHandler &window, Addr window_base)
    : QueuedResponsePort(name, queue),
      owner(owner),
      queue(owner, *this),
      window(window),
      windowBase(window_base)
{
}

CtAccessResult
CtPioPort::doAccess(PacketPtr pkt, Tick now)
{
    CtAccess a;
    a.base = pkt->getAddr();
    a.data = pkt->getPtr<uint8_t>();
    a.write = pkt->isWrite();
    /** todo 后续需要根据外部 pkt 的实际情况进行替换 */
    a.initiator = CtInitiator::MT;
    a.size = pkt->getSize();
    /** 调用对应 windows 接口访问 */
    return window.access(a, now);
}

/** 仿照 mgmt 逻辑 */
AddrRangeList
CtPioPort::getAddrRanges() const
{
    AddrRangeList r;
    r.push_back(RangeSize(windowBase, CtWindowSize));
    return r;
}

/** pio 接收逻辑实现 */
/** 时序调用，无论成功失败都需要按周期数进行返回处理 */
bool
CtPioPort::recvTimingReq(PacketPtr pkt)
{
    Tick now = owner.clockEdge();
    CtAccessResult res = doAccess(pkt, now);
    pkt->makeResponse();
    if (res.status != CtStatus::Ok) {
        /** 失败调用 pkt 函数进行返回并在下一拍发回错误响应 */
        pkt->setBadAddress();
        schedTimingResp(pkt, owner.clockEdge(Cycles(1)));
    } else {
        /** 成功正常发回计算好了 delay */
        schedTimingResp(pkt, now + res.delay);
    }
    return true;
}

/** 原子调用，只返回耗时 */
Tick
CtPioPort::recvAtomic(PacketPtr pkt)
{
    CtAccessResult res = doAccess(pkt, owner.clockEdge());
    pkt->makeResponse();
    if (res.status != CtStatus::Ok) {
        pkt->setBadAddress();
        return owner.clockPeriod();
    }
    return res.delay;
}

void
CtPioPort::recvFunctional(PacketPtr pkt)
{
    CtAccessResult res = doAccess(pkt, curTick());
    pkt->makeResponse();
    if (res.status != CtStatus::Ok) {
        pkt->setBadAddress();
    }
}

/** CtDmaPort 的端口实现 */
CtDmaPort::CtDmaPort(const std::string &name, ClockedObject &owner)
    : QueuedRequestPort(name, queue, snoop_queue),
      owner(owner),
      queue(owner,*this),
      snoop_queue(owner, *this)
{
}

bool
CtDmaPort::canSend()const{
    /** 初级的简单版本，在后续应该去进行队列的添加，QueuedRequestPort 不能解决所有问题 */
    return true;
}

/** 发送方法的核心实现 */
void
CtDmaPort::sendRead(Addr dpa, unsigned len, CtNocReadDone done){
    auto *back = new Callback();
    back->read = std::move(done);
    sendPacket(dpa, nullptr, len, false, back);
}

void
CtDmaPort::sendWrite(Addr dpa, unsigned len, const uint8_t *data,CtNocWriteDone done){
    auto *back = new Callback();
    back->write = std::move(done);
    sendPacket(dpa,data,len,true,back);
}

/** 接收方法的最新实现  */
bool
CtDmaPort::recvTimingResp(PacketPtr pkt)
{
    /** 从返回的 pkt 上弹出栈顶的 SendeerState，并向下转型还原成 CallbackState* */
    auto *back = dynamic_cast<Callback *>(pkt->popSenderState());
    CtStatus st = pkt->isError() ? CtStatus::Reject : CtStatus::Ok;

    /** 通知nidma */
    if (back != nullptr) {
        if (back->read) {
            back->read(st, pkt->getConstPtr<uint8_t>());
        }
        if (back->write) {
            back->write(st);
        }
        delete back;
    }

    delete pkt;
    return true;
}

void
CtDmaPort::sendPacket(Addr dpa, const uint8_t *data, unsigned len, bool is_write, Callback *back){
    /** packet 对象的创建 */
    RequestPtr req = std::make_shared<Request>(dpa, len, 0, Request::funcRequestorId);
    MemCmd cmd = is_write ? MemCmd::WriteReq : MemCmd::ReadReq;
    PacketPtr pkt = new Packet(req, cmd);
    /** 在堆上开辟数据区 */
    pkt->allocate();
    if (is_write && data != nullptr) {
        // 如果是写请求，把要写的数据 memcpy 进刚刚 allocate 出来的内部缓冲区里
        pkt->setData(data);
    }

    /** 将 back 压入栈顶，通过栈的形式来处理跨周期的回调调用 */
    pkt->pushSenderState(back);
    schedTimingReq(pkt, owner.clockEdge());
}

/** 主类函数,初始化顺序要求和头文件中严格保证相同，分块展示也和头文件中保持一致 */
Gw100Ct::Gw100Ct(const Params &p)
    : ClockedObject(p),
      windowBase(p.base_addr + p.tile_id * p.window_size),
      clock(*this, [this] { tick(); }),

      sram(clock),
      sync(clock),
      ctrl_ram(clock),
      ct_fc32(),

      window(sram, sync,ctrl_ram, windowBase),
      pio(name() + ".pio", *this, window, windowBase),
      dma_port(name()+".request",*this),

      ct_nidma(clock,dma_port,window),
      ct_matrix(clock,window,sync,ct_fc32),
      ct_vector(clock,window,sync,ct_fc32),
      ct_ctrl(clock,ct_nidma,ct_matrix,ct_vector,ctrl_ram)
{
}

void
Gw100Ct::tick()
{
    ct_nidma.step();
    ct_ctrl.step();
}

Port &
Gw100Ct::getPort(const std::string &if_name, PortID idx)
{
    if (if_name == "pio") {
    return pio;
    } else if (if_name == "dma_port") { 
        return dma_port;
    }
    return ClockedObject::getPort(if_name, idx);
}

void
Gw100Ct::init()
{
    ClockedObject::init();
    fatal_if(!pio.isConnected(), "%s: pio 端口没有连接", name());
    pio.sendRangeChange();
}

} 
} 
