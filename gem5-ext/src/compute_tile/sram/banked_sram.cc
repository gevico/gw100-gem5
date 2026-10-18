/**
 * @file  banked_sram.cc
 * @brief BankedSram 的实现
 *
 * @see SPEC-200
 */

#include "src/compute_tile/sram/banked_sram.hh"

#include <algorithm>

#include "base/cprintf.hh"
#include "base/logging.hh"

namespace gem5
{
namespace gw100
{

BankedSram::SramPort::SramPort(const std::string &name, BankedSram &m, PortID id)
    : QueuedResponsePort(name, queue, id), queue(m, *this), mem(m)
{
}

Tick
BankedSram::SramPort::recvAtomic(PacketPtr pkt)
{
    mem.access(pkt);
    return mem.cyclesToTicks(mem.latency);
}

void
BankedSram::SramPort::recvFunctional(PacketPtr pkt)
{
    pkt->pushLabel(mem.name());
    if (!queue.trySatisfyFunctional(pkt))
        mem.functionalAccess(pkt);
    pkt->popLabel();
}

bool
BankedSram::SramPort::recvTimingReq(PacketPtr pkt)
{
    return mem.handleTiming(pkt, *this, getId());
}

AddrRangeList
BankedSram::SramPort::getAddrRanges() const
{
    return {mem.getAddrRange()};
}

BankedSram::BankedSram(const Params &p)
    : memory::AbstractMemory(p),
      numBanks(p.banks), bankBytes(p.bank_bytes), latency(p.latency),
      bankFree(p.banks, 0), portFree(p.port_port_connection_count, 0), sramStats(*this)
{
    fatal_if(numBanks == 0 || bankBytes == 0, "%s: bank 数和 bank 宽度必须大于 0", name());
    for (int i = 0; i < p.port_port_connection_count; ++i) {
        ports.emplace_back(std::make_unique<SramPort>(
            csprintf("%s.port[%d]", name(), i), *this, i));
    }
}

void
BankedSram::init()
{
    memory::AbstractMemory::init();
    for (auto &p : ports) {
        fatal_if(!p->isConnected(), "%s 没有连接", p->name());
        p->sendRangeChange();
    }
}

Port &
BankedSram::getPort(const std::string &if_name, PortID idx)
{
    if (if_name == "port" && idx >= 0 && idx < static_cast<PortID>(ports.size()))
        return *ports[idx];
    return memory::AbstractMemory::getPort(if_name, idx);
}

bool
BankedSram::handleTiming(PacketPtr pkt, SramPort &port, PortID idx)
{
    panic_if(!(pkt->isRead() || pkt->isWrite()), "%s: 只支持读写，收到 %s", name(),
             pkt->cmdString());

    const Tick arrive = curTick() + pkt->headerDelay + pkt->payloadDelay;
    pkt->headerDelay = pkt->payloadDelay = 0;

    const Tick period = clockPeriod();
    const Addr off = pkt->getAddr() - range.start();
    const Addr first = off / bankBytes;
    const Addr last = (off + pkt->getSize() - 1) / bankBytes;

    Tick start = std::max(arrive, portFree[idx]);
    const Tick no_conflict = start;
    for (Addr b = first; b <= last; ++b)
        start = std::max(start, bankFree[b % numBanks]);
    for (Addr b = first; b <= last; ++b)
        bankFree[b % numBanks] = start + period;
    portFree[idx] = start + period;

    sramStats.requests++;
    sramStats.bytes += pkt->getSize();
    sramStats.conflictCycles += (start - no_conflict) / period;

    const bool needs_resp = pkt->needsResponse();
    access(pkt);
    if (needs_resp)
        port.schedTimingResp(pkt, start + cyclesToTicks(latency));
    else
        delete pkt;
    return true;
}

BankedSram::SramStats::SramStats(BankedSram &mem)
    : statistics::Group(&mem, "sram"),
      ADD_STAT(requests, statistics::units::Count::get(), "收到的请求数"),
      ADD_STAT(bytes, statistics::units::Byte::get(), "读写的字节数"),
      ADD_STAT(conflictCycles, statistics::units::Cycle::get(),
               "因端口或 bank 冲突被推迟的周期数")
{
}

} // namespace gw100
} // namespace gem5
