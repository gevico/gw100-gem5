/**
 * @file  sram_probe.cc
 * @brief SRAM 多入口、ECC、反压、保持与文件恢复验证
 * @see spec-sram
 */
#include "tests/integration/sram/sram_probe.hh"
#include <iostream>
#include "base/logging.hh"
#include "mem/packet.hh"
#include "sim/clock_domain.hh"
#include "sim/serialize.hh"
#include "sim/sim_exit.hh"

namespace gem5::gw100
{
/** 创建有限来源、返回阻塞与生命周期注入事件。 */
SramProbe::SramProbe(const Params &p)
    : ClockedObject(p),
      sram(*p.sram),
      edge([this] { process(); }, name() + ".edge"),
      resetEdge([this] { sram.resetMemory(true); }, name() + ".reset", false,
                Event::CPU_Tick_Pri + 9),
      pauseEdge([this] { sram.pauseMemory(); }, name() + ".pause", false,
                Event::CPU_Switch_Pri),
      resumeEdge(
          [this] {
              auto domain =
                  dynamic_cast<SrcClockDomain *>(params().clk_domain);
              fatal_if(!domain, "SRAM 夹具需要可切换时钟域");
              domain->perfLevel(0);
              sram.resumeMemory();
              if (edge.scheduled()) {
                  deschedule(edge);
              }
              schedule(edge, clockEdge(Cycles(1)));
          },
          name() + ".resume", false, Event::CPU_Switch_Pri),
      snapshot(p.snapshot),
      retention(p.retention),
      stallCycles(p.stall_cycles),
      resetTest(p.reset_test),
      clockSwitch(p.clock_switch)
{
    for (unsigned source = 0; source < sram.model().config().sources;
         ++source) {
        ports.push_back(std::make_unique<ProbePort>(
            name() + ".ports[" + std::to_string(source) + "]", *this, source));
    }
}
/** 返回指定来源的请求端口。 */
Port &
SramProbe::getPort(const std::string &name, PortID index)
{
    if (name == "ports") {
        return *ports.at(index);
    }
    return ClockedObject::getPort(name, index);
}
/** 初始化新实例或继续恢复后的测试阶段。 */
void
SramProbe::startup()
{
    if (sram.model().state() == SramModel::State::Reset) {
        sram.initialize();
    }
    schedule(edge, clockEdge(Cycles(1)));
}
/** 生成来源及索引对应的全局 SRAM 地址。 */
uint64_t
SramProbe::address(unsigned source, unsigned index) const
{ return sram.base() + source * 4096 + index * 64; }
/** 独立生成初始化和请求模式对应的数据期望。 */
uint8_t
SramProbe::expected(unsigned source, unsigned index, unsigned byte) const
{
    const unsigned sizes[] = {1, 2, 4, 8, 17, 64};
    return byte < sizes[index % 6] ? uint8_t(source * 31 + index + byte + 1)
                                   : 0;
}
/** 为一个来源发出或重试唯一待发请求。 */
void
SramProbe::send(ProbePort &port)
{
    unsigned count = phase <= 2 || phase == 7 ? Count : 1;
    if (port.waiting || port.issued == count || port.outstanding == 16) {
        return;
    }
    if (!port.pending) {
        unsigned index = port.issued;
        bool write = phase == 1 || phase == 5 || phase == 10;
        unsigned sizes[] = {1, 2, 4, 8, 17, 64};
        unsigned size = write ? (phase != 1 ? 8 : sizes[index % 6]) : 64;
        auto req = std::make_shared<Request>(address(port.source, index), size,
                                             0, Request::funcRequestorId);
        port.pending =
            new Packet(req, write ? MemCmd::WriteReq : MemCmd::ReadReq);
        port.pending->allocate();
        port.pending->headerDelay = index % 3 == 0   ? 0
                                    : index % 3 == 1 ? 1
                                                     : clockPeriod() - 1;
        port.pending->payloadDelay = write ? clockPeriod() : 0;
        for (unsigned i = 0; i < size; ++i) {
            port.pending->getPtr<uint8_t>()[i] =
                expected(port.source, index, i);
        }
        port.pending->pushSenderState(new Tag(index, phase));
    }
    if (port.sendTimingReq(port.pending)) {
        ++port.issued;
        ++port.outstanding;
        ++accepted;
        fatal_if(sram.sourceDrained(port.source),
                 "已接受的入口请求未计入来源排空");
        port.pending = nullptr;
    } else {
        ++retries;
        port.waiting = true;
    }
}
/** 注入一次返回反压，其余响应交给数据检查。 */
bool
SramProbe::ProbePort::recvTimingResp(PacketPtr packet)
{
    if (!stalled && probe.stallCycles) {
        stalled = blocked = true;
        release = curTick() + probe.cyclesToTicks(Cycles(probe.stallCycles));
        return false;
    }
    probe.received(*this, packet);
    return true;
}
/** 独立核对错误状态及每个读取字节。 */
void
SramProbe::received(ProbePort &port, PacketPtr packet)
{
    auto tag = static_cast<Tag *>(packet->popSenderState());
    bool fault = tag->phase == 4 || tag->phase == 10;
    fatal_if(packet->isError() != fault, "SRAM 夹具错误状态不匹配 phase=%u",
             tag->phase);
    if (!fault && packet->isRead()) {
        for (unsigned i = 0; i < packet->getSize(); ++i) {
            fatal_if(packet->getConstPtr<uint8_t>()[i] !=
                         expected(port.source, tag->index, i),
                     "SRAM 数据错误 source=%u index=%u byte=%u", port.source,
                     tag->index, i);
        }
    }
    --port.outstanding;
    delete tag;
    delete packet;
}
/** 逐周期推进流量、返回释放及测试阶段。 */
void
SramProbe::process()
{
    if (sram.model().isPaused()) {
        schedule(edge, clockEdge(Cycles(1)));
        return;
    }
    for (auto &port : ports) {
        if (port->blocked && curTick() >= port->release) {
            port->blocked = false;
            port->sendRetryResp();
        }
    }
    if (!phase && sram.model().state() == SramModel::State::Ready) {
        phase = resetTest ? 10 : 1;
    }
    if (phase == 7 && sram.model().state() == SramModel::State::Retention) {
        sram.leaveRetention();
    }
    if (phase) {
        for (auto &port : ports) {
            send(*port);
        }
        if (phase == 10 && !resetInjected) {
            resetInjected = true;
            // 写在下一拍发出，在提交边沿之前按复位优先级取消。
            schedule(resetEdge, clockEdge(Cycles(2)));
        }
        if (clockSwitch && phase == 1 && !switchStarted) {
            switchStarted = true;
            Tick at = clockEdge(Cycles(2));
            schedule(pauseEdge, at);
            schedule(resumeEdge, at + 84000);
        }
        unsigned count = phase <= 2 || phase == 7 ? Count : 1;
        bool done = sram.drainedMemory();
        for (const auto &port : ports) {
            done &= port->issued == count && !port->outstanding;
        }
        if (done) {
            for (auto &port : ports) {
                port->issued = 0;
                port->waiting = false;
            }
            if (phase == 10) {
                for (unsigned s = 0; s < ports.size(); ++s) {
                    uint8_t data[8];
                    sram.model().functionalRead(address(s, 0) - sram.base(),
                                                data, 8);
                    for (auto value : data) {
                        fatal_if(value, "复位后旧写仍然生效");
                    }
                }
                phase = 1;
            } else {
                ++phase;
            }
            if (phase == 3 || phase == 4) {
                for (unsigned s = 0; s < ports.size(); ++s) {
                    sram.model().inject(address(s, 0) - sram.base(),
                                        phase - 3);
                }
            }
            if (phase == 6) {
                fatal_if(sram.model().stats().ce < ports.size() ||
                             sram.model().stats().ue < ports.size(),
                         "SRAM ECC 路径没有执行");
                phase = 7;
                if (retention) {
                    fatal_if(!sram.enterRetention(), "SRAM 保持进入失败");
                }
                if (snapshot) {
                    exitSimLoop("sram checkpoint");
                }
            } else if (phase == 8) {
                std::cout << "SRAM_PROBE_PASS accepted=" << accepted
                          << " retries=" << retries
                          << " ce=" << sram.model().stats().ce
                          << " ue=" << sram.model().stats().ue << '\n';
                exitSimLoop("sram pass");
                return;
            }
        }
    }
    schedule(edge, clockEdge(Cycles(1)));
}
/** 在保存边界停止新流量并等待已接受操作。 */
DrainState
SramProbe::drain()
{
    draining = true;
    if (edge.scheduled()) {
        deschedule(edge);
    }
    return DrainState::Drained;
}
/** checkpoint 后恢复分阶段流量。 */
void
SramProbe::drainResume()
{
    draining = false;
    if (!edge.scheduled()) {
        schedule(edge, clockEdge(Cycles(1)));
    }
}
/** 保存测试阶段及接受、retry 计数。 */
void
SramProbe::serialize(CheckpointOut &cp) const
{
    SERIALIZE_SCALAR(phase);
    SERIALIZE_SCALAR(accepted);
    SERIALIZE_SCALAR(retries);
}
/** 恢复下一阶段所需的夹具状态。 */
void
SramProbe::unserialize(CheckpointIn &cp)
{
    UNSERIALIZE_SCALAR(phase);
    UNSERIALIZE_SCALAR(accepted);
    UNSERIALIZE_SCALAR(retries);
    for (auto &port : ports) {
        port->stalled = true;
    }
}
} // namespace gem5::gw100
