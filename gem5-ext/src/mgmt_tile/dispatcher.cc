/**
 * @file  dispatcher.cc
 * @brief Dispatcher 的实现
 *
 * @see SPEC-330, SPEC-400
 */

#include "src/mgmt_tile/dispatcher.hh"

#include <cstring>

#include "base/cprintf.hh"
#include "base/intmath.hh"
#include "base/logging.hh"
#include "base/trace.hh"
#include "debug/AcisDispatch.hh"
#include "mem/packet_access.hh"
#include "src/compute_tile/controller/tile_controller.hh"

namespace gem5
{
namespace acis
{

namespace
{

template <typename T>
T
rd(const uint8_t *p, unsigned off)
{
    T v;
    std::memcpy(&v, p + off, sizeof(T));
    return v;
}

template <typename T>
void
wr(uint8_t *p, unsigned off, T v)
{
    std::memcpy(p + off, &v, sizeof(T));
}

} // anonymous namespace

Dispatcher::Dispatcher(const Params &p)
    : DmaDevice(p), pioAddr(p.pio_addr), pioDelay(p.pio_latency),
      ctrls(p.controllers.begin(), p.controllers.end()),
      instLatency(p.inst_latency), instBytesPerCycle(p.inst_bytes_per_cycle),
      cmdEvent([this]() { commandFetched(); }, name() + ".cmd"),
      progEvent([this]() { programFetched(); }, name() + ".prog"),
      cplEvent([this]() { completionWritten(); }, name() + ".cpl"),
      dispatchEvent([this]() { dispatch(); }, name() + ".dispatch"),
      dispStats(*this)
{
    fatal_if(ctrls.size() > 64, "%s: tile_mask 只有 64 位，最多 64 个 Compute Tile", name());
    fatal_if(instBytesPerCycle == 0, "%s: inst_bytes_per_cycle 必须大于 0", name());
}

void
Dispatcher::init()
{
    DmaDevice::init();
    for (auto *c : ctrls)
        c->setDispatcher(this);
}

AddrRangeList
Dispatcher::getAddrRanges() const
{
    return {RangeSize(pioAddr, isa::mgmt_reg::size)};
}

Tick
Dispatcher::read(PacketPtr pkt)
{
    const Addr off = pkt->getAddr() - pioAddr;
    uint64_t v = 0;
    switch (off) {
      case isa::mgmt_reg::CMD_ADDR: v = cmdAddr; break;
      case isa::mgmt_reg::CMD_ADDR + 4: v = cmdAddr >> 32; break;
      case isa::mgmt_reg::STATUS: v = status; break;
      case isa::mgmt_reg::VERSION: v = 1; break;
      default:
        warn("%s: 读未定义的寄存器偏移 %#x", name(), off);
    }
    if (pkt->getSize() == 8)
        pkt->setLE<uint64_t>(v);
    else
        pkt->setLE<uint32_t>(static_cast<uint32_t>(v));
    pkt->makeAtomicResponse();
    return pioDelay;
}

Tick
Dispatcher::write(PacketPtr pkt)
{
    const Addr off = pkt->getAddr() - pioAddr;
    const uint64_t v = pkt->getSize() == 8 ? pkt->getLE<uint64_t>() : pkt->getLE<uint32_t>();
    switch (off) {
      case isa::mgmt_reg::CMD_ADDR:
        if (pkt->getSize() == 8)
            cmdAddr = v;
        else
            cmdAddr = (cmdAddr & ~0xffffffffULL) | v;
        break;
      case isa::mgmt_reg::CMD_ADDR + 4:
        cmdAddr = (cmdAddr & 0xffffffffULL) | (v << 32);
        break;
      case isa::mgmt_reg::DOORBELL:
        start();
        break;
      default:
        warn("%s: 写未定义的寄存器偏移 %#x", name(), off);
    }
    pkt->makeAtomicResponse();
    return pioDelay;
}

void
Dispatcher::start()
{
    if (status == Busy) {
        warn("%s: 上一条命令尚未完成，忽略这次 doorbell", name());
        return;
    }
    status = Busy;
    startTick = curTick();
    dispStats.commands++;
    DPRINTF(AcisDispatch, "doorbell：读命令记录 %#x\n", cmdAddr);
    dmaRead(cmdAddr, isa::cmd_rec::bytes, &cmdEvent, cmdBuf);
}

void
Dispatcher::commandFetched()
{
    programAddr = rd<uint64_t>(cmdBuf, isa::cmd_rec::program_addr);
    programBytes = rd<uint32_t>(cmdBuf, isa::cmd_rec::program_bytes);
    completionAddr = rd<uint64_t>(cmdBuf, isa::cmd_rec::completion_addr);
    tag = rd<uint64_t>(cmdBuf, isa::cmd_rec::tag);
    if (programBytes < isa::prog::header_bytes) {
        complete(Error, "程序长度小于程序头");
        return;
    }
    progBuf.assign(programBytes, 0);
    DPRINTF(AcisDispatch, "读程序 %#x，%u 字节\n", programAddr, programBytes);
    dmaRead(programAddr, programBytes, &progEvent, progBuf.data());
}

void
Dispatcher::programFetched()
{
    const uint8_t *p = progBuf.data();
    if (rd<uint32_t>(p, 0) != isa::prog::magic) {
        complete(Error, "程序头的 magic 不对");
        return;
    }
    const uint32_t n = rd<uint32_t>(p, 8);
    if (isa::prog::header_bytes + uint64_t(n) * isa::prog::entry_bytes > programBytes) {
        complete(Error, "程序长度与条目数不符");
        return;
    }

    entries.clear();
    uint64_t used = 0;
    endMask = 0;
    for (uint32_t i = 0; i < n; ++i) {
        const uint8_t *e = p + isa::prog::header_bytes + i * isa::prog::entry_bytes;
        Entry ent;
        ent.mask = rd<uint64_t>(e, 0);
        std::string err;
        if (!decode(e + 8, ent.inst, err)) {
            complete(Error, csprintf("第 %u 条指令：%s", i, err));
            return;
        }
        if (!isa::isImplemented(ent.inst.opcode)) {
            complete(Error, csprintf("第 %u 条指令 %s 尚未实现", i, opcodeName(ent.inst.opcode)));
            return;
        }
        if (ent.mask == 0 || (ctrls.size() < 64 && (ent.mask >> ctrls.size()))) {
            complete(Error, csprintf("第 %u 条指令的 tile_mask %#x 非法", i, ent.mask));
            return;
        }
        // Tile 相对地址按每个目标 Tile 分别校验
        for (unsigned t = 0; t < ctrls.size(); ++t) {
            if (!((ent.mask >> t) & 1))
                continue;
            TensorInst local = ent.inst;
            if (!ctrls[t]->resolve(local, err)) {
                complete(Error, csprintf("第 %u 条指令在 Tile %u 上：%s", i, t, err));
                return;
            }
        }
        used |= ent.mask;
        if (ent.inst.opcode == isa::Opcode::END)
            endMask |= ent.mask;
        entries.push_back(ent);
    }
    if (entries.empty() || endMask != used) {
        complete(Error, "每个参与执行的 Tile 都必须以 END 结束");
        return;
    }

    next = 0;
    doneMask = 0;
    channelFree = curTick();
    dispatch();
}

void
Dispatcher::dispatch()
{
    waitingCredit = false;
    const Cycles occupancy(divCeil(isa::kInstBytes, instBytesPerCycle));
    while (next < entries.size()) {
        if (channelFree > curTick()) {
            if (!dispatchEvent.scheduled())
                schedule(dispatchEvent, channelFree);
            return;
        }
        const Entry &e = entries[next];
        const isa::Unit unit = e.inst.unit();
        for (unsigned t = 0; t < ctrls.size(); ++t) {
            if ((e.mask >> t) & 1 && !ctrls[t]->canReserve(unit)) {
                waitingCredit = true;
                dispStats.creditStalls++;
                return;
            }
        }
        for (unsigned t = 0; t < ctrls.size(); ++t) {
            if ((e.mask >> t) & 1)
                ctrls[t]->reserve(unit);
        }
        channelFree = curTick() + cyclesToTicks(occupancy);
        const Tick arrive = channelFree + cyclesToTicks(instLatency);
        const Entry ent = e;
        schedule(new EventFunctionWrapper(
                     [this, ent]() {
                         for (unsigned t = 0; t < ctrls.size(); ++t) {
                             if ((ent.mask >> t) & 1)
                                 ctrls[t]->deliver(ent.inst);
                         }
                     },
                     name() + ".deliver", true),
                 arrive);
        dispStats.instsDispatched++;
        next++;
    }
    checkComplete();
}

void
Dispatcher::creditAvailable()
{
    if (waitingCredit && !dispatchEvent.scheduled())
        schedule(dispatchEvent, clockEdge(Cycles(1)));
}

void
Dispatcher::tileDone(unsigned tile)
{
    DPRINTF(AcisDispatch, "Tile %u 完成\n", tile);
    doneMask |= 1ULL << tile;
    checkComplete();
}

void
Dispatcher::checkComplete()
{
    if (status == Busy && next == entries.size() && !entries.empty() && doneMask == endMask)
        complete(Done, "");
}

void
Dispatcher::complete(Status st, const std::string &why)
{
    if (st == Error)
        warn("%s: 命令执行失败：%s", name(), why);
    endTick = curTick();
    dispStats.runTicks += endTick - startTick;
    pendingStatus = st;
    entries.clear();
    next = 0;
    std::memset(cplBuf, 0, sizeof(cplBuf));
    wr<uint32_t>(cplBuf, isa::cpl_rec::status, st);
    wr<uint64_t>(cplBuf, isa::cpl_rec::tag, tag);
    wr<uint64_t>(cplBuf, isa::cpl_rec::start_tick, startTick);
    wr<uint64_t>(cplBuf, isa::cpl_rec::end_tick, endTick);
    DPRINTF(AcisDispatch, "完成，状态 %u，用时 %llu ticks\n", st, endTick - startTick);
    dmaWrite(completionAddr, isa::cpl_rec::bytes, &cplEvent, cplBuf);
}

void
Dispatcher::completionWritten()
{
    status = pendingStatus;
}

Dispatcher::DispStats::DispStats(Dispatcher &d)
    : statistics::Group(&d, "dispatch"),
      ADD_STAT(commands, statistics::units::Count::get(), "收到的命令数"),
      ADD_STAT(instsDispatched, statistics::units::Count::get(), "下发的指令条目数"),
      ADD_STAT(creditStalls, statistics::units::Count::get(), "因目标队列已满而暂停的次数"),
      ADD_STAT(runTicks, statistics::units::Tick::get(), "从 doorbell 到全部完成的时间")
{
}

} // namespace acis
} // namespace gem5
