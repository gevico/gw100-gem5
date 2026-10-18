/**
 * @file  test_host.cc
 * @brief TestHost 的实现
 *
 * @see SPEC-400
 */

#include "src/testhost/test_host.hh"

#include <cstring>
#include <fstream>
#include <iterator>

#include "base/logging.hh"
#include "mem/packet_access.hh"
#include "mem/request.hh"
#include "sim/sim_exit.hh"
#include "sim/system.hh"
#include "src/common/fc.hh"

namespace gem5
{
namespace gw100
{

namespace
{

/** 解析 "addr=path" 或 "addr:bytes=path"；地址和长度支持 0x 前缀。 */
void
parseSpec(const std::string &spec, Addr &addr, uint64_t &bytes, std::string &path)
{
    const auto eq = spec.find('=');
    fatal_if(eq == std::string::npos, "TestHost: 格式应为 addr[:bytes]=path，收到 '%s'", spec);
    const std::string head = spec.substr(0, eq);
    path = spec.substr(eq + 1);
    const auto colon = head.find(':');
    addr = std::stoull(head.substr(0, colon), nullptr, 0);
    bytes = colon == std::string::npos ? 0 : std::stoull(head.substr(colon + 1), nullptr, 0);
}

} // anonymous namespace

TestHost::TestHost(const Params &p)
    : ClockedObject(p), port(name() + ".port", *this),
      requestorId(p.system->getRequestorId(this)), loads(p.loads), dumps(p.dumps),
      mgmtBase(p.mgmt_base), cmdRecordAddr(p.cmd_record_addr),
      completionAddr(p.completion_addr), pollInterval(p.poll_interval),
      timeoutTicks(p.timeout), resultFile(p.result_file), chunkBytes(p.backdoor_chunk_bytes),
      kickEvent([this]() { writeReg(mgmtBase + isa::mgmt_reg::CMD_ADDR, cmdRecordAddr, 8); },
                name() + ".kick"),
      pollEvent([this]() { poll(); }, name() + ".poll"),
      timeoutEvent([this]() { timeout(); }, name() + ".timeout")
{
    fatal_if(chunkBytes == 0 || (chunkBytes & (chunkBytes - 1)),
             "%s: backdoor_chunk_bytes 必须是 2 的幂", name());
}

Port &
TestHost::getPort(const std::string &if_name, PortID idx)
{
    if (if_name == "port")
        return port;
    return ClockedObject::getPort(if_name, idx);
}

void
TestHost::startup()
{
    ClockedObject::startup();
    loadAll();
    schedule(kickEvent, clockEdge(Cycles(1)));
    if (timeoutTicks > 0)
        schedule(timeoutEvent, curTick() + timeoutTicks);
}

void
TestHost::functionalAccess(Addr addr, uint8_t *data, uint64_t bytes, bool write)
{
    // 按 chunkBytes 对齐切分：DRAM 按 256 B 在通道间交织，一个功能访问不能跨通道
    for (uint64_t off = 0; off < bytes;) {
        const Addr a = addr + off;
        const uint64_t to_boundary = chunkBytes - (a & (chunkBytes - 1));
        const unsigned len = static_cast<unsigned>(std::min(to_boundary, bytes - off));
        auto req = std::make_shared<Request>(a, len, 0, requestorId);
        Packet pkt(req, write ? MemCmd::WriteReq : MemCmd::ReadReq);
        pkt.dataStatic(data + off);
        port.sendFunctional(&pkt);
        off += len;
    }
}

void
TestHost::loadAll()
{
    for (const auto &spec : loads) {
        Addr addr;
        uint64_t unused;
        std::string path;
        parseSpec(spec, addr, unused, path);
        std::ifstream in(path, std::ios::binary);
        fatal_if(!in, "%s: 打不开输入文件 %s", name(), path);
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)),
                                  std::istreambuf_iterator<char>());
        inform("%s: 加载 %s（%llu 字节）到 %#x", name(), path, data.size(), addr);
        functionalAccess(addr, data.data(), data.size(), true);
    }
}

void
TestHost::sendTiming(PacketPtr pkt)
{
    panic_if(blockedPkt, "%s: 上一个请求还没发出去", name());
    if (!port.sendTimingReq(pkt))
        blockedPkt = pkt;
}

void
TestHost::retry()
{
    PacketPtr pkt = blockedPkt;
    blockedPkt = nullptr;
    sendTiming(pkt);
}

void
TestHost::writeReg(Addr addr, uint64_t value, unsigned bytes)
{
    auto req = std::make_shared<Request>(addr, bytes, Request::UNCACHEABLE, requestorId);
    PacketPtr pkt = Packet::createWrite(req);
    pkt->allocate();
    if (bytes == 8)
        pkt->setLE<uint64_t>(value);
    else
        pkt->setLE<uint32_t>(static_cast<uint32_t>(value));
    sendTiming(pkt);
}

void
TestHost::poll()
{
    auto req = std::make_shared<Request>(completionAddr, isa::cpl_rec::bytes, 0, requestorId);
    PacketPtr pkt = Packet::createRead(req);
    pkt->allocate();
    sendTiming(pkt);
}

bool
TestHost::handleResp(PacketPtr pkt)
{
    panic_if(pkt->isError(), "%s: 访问 %#x 出错", name(), pkt->getAddr());
    switch (phase) {
      case Phase::WriteCmdAddr:
        delete pkt;
        phase = Phase::Doorbell;
        writeReg(mgmtBase + isa::mgmt_reg::DOORBELL, 1, 4);
        break;
      case Phase::Doorbell:
        delete pkt;
        phase = Phase::Polling;
        schedule(pollEvent, curTick() + pollInterval);
        break;
      case Phase::Polling: {
        uint8_t cpl[isa::cpl_rec::bytes];
        pkt->writeData(cpl);
        delete pkt;
        uint32_t status;
        std::memcpy(&status, cpl + isa::cpl_rec::status, sizeof(status));
        if (status == 0)
            schedule(pollEvent, curTick() + pollInterval);
        else
            finishRun(cpl);
        break;
      }
      case Phase::Finished:
        delete pkt;
        break;
    }
    return true;
}

void
TestHost::finishRun(const uint8_t *cpl)
{
    phase = Phase::Finished;
    if (timeoutEvent.scheduled())
        deschedule(timeoutEvent);

    uint32_t status;
    uint64_t tag, start, end;
    std::memcpy(&status, cpl + isa::cpl_rec::status, sizeof(status));
    std::memcpy(&tag, cpl + isa::cpl_rec::tag, sizeof(tag));
    std::memcpy(&start, cpl + isa::cpl_rec::start_tick, sizeof(start));
    std::memcpy(&end, cpl + isa::cpl_rec::end_tick, sizeof(end));

    for (const auto &spec : dumps) {
        Addr addr;
        uint64_t bytes;
        std::string path;
        parseSpec(spec, addr, bytes, path);
        std::vector<uint8_t> data(bytes);
        functionalAccess(addr, data.data(), bytes, false);
        std::ofstream out(path, std::ios::binary);
        fatal_if(!out, "%s: 写不了输出文件 %s", name(), path);
        out.write(reinterpret_cast<const char *>(data.data()), data.size());
    }

    if (!resultFile.empty()) {
        std::ofstream out(resultFile);
        out << "{\"status\": " << status << ", \"tag\": " << tag << ", \"start_tick\": "
            << start << ", \"end_tick\": " << end << ", \"device_ticks\": " << (end - start)
            << ", \"sim_tick\": " << curTick() << ", \"tick_freq\": " << sim_clock::Frequency
            << "}\n";
    }
    inform("%s: 完成，状态 %u，设备用时 %llu ticks", name(), status, end - start);
    exitSimLoop("gw100 testhost done", status == 2 ? 0 : 1);
}

void
TestHost::timeout()
{
    warn("%s: 超时，命令在 %llu ticks 内没有完成", name(), timeoutTicks);
    exitSimLoop("gw100 testhost timeout", 2);
}

} // namespace gw100
} // namespace gem5
