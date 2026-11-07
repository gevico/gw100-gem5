/**
 * @file  memory_probe.cc
 * @brief Gw100MemoryProbe 的实现
 *
 * @see spec-dram
 */
#include "tests/integration/memory/memory_probe.hh"

#include "sim/sim_exit.hh"
#include "sim/system.hh"
#include "src/gw100/mem_tile/memory_ingress.hh"
#include "src/gw100/mem_tile/memory_tile.hh"

namespace gem5::gw100
{

namespace md = memorydefs;

/** 绑定测试源和来源编号 */
Gw100MemoryProbe::ProbePort::ProbePort(const std::string &name,
                                       Gw100MemoryProbe &owner)
    : RequestPort(name),
      owner(owner),
      retryResponse([this] { sendRetryResp(); }, name + ".retryResponse")
{}

/** 接收并比较响应 */
bool
Gw100MemoryProbe::ProbePort::recvTimingResp(PacketPtr pkt)
{
    if (owner.responseHold && heldResponse != pkt) {
        panic_if(heldResponse, "Probe received two blocked responses");
        heldResponse = pkt;
        owner.schedule(retryResponse, curTick() + owner.responseHold);
        return false;
    }
    heldResponse = nullptr;
    owner.response(pkt);
    return true;
}

/** 重试被拒绝的请求 */
void
Gw100MemoryProbe::ProbePort::recvReqRetry()
{
    panic_if(!blocked, "Probe received an unexpected retry");
    if (sendTimingReq(blocked)) {
        blocked = nullptr;
    }
    owner.wake();
}

/** 绑定测试操作和三个硬件来源 */
Gw100MemoryProbe::Gw100MemoryProbe(const Params &p)
    : ClockedObject(p),
      operations(p.operations),
      maximum(p.max_outstanding),
      responseHold(p.response_hold),
      requestor(p.system->getRequestorId(this)),
      event([this] { process(); }, name() + ".process")
{
    fatal_if(operations.size() % 5, "Invalid memory probe script");
    for (unsigned i = 0; i < 3; ++i) {
        ports[i] = std::make_unique<ProbePort>(
            name() + ".source" + std::to_string(i), *this);
    }
}

/** 查找三个来源端口 */
Port &
Gw100MemoryProbe::getPort(const std::string &name, PortID idx)
{
    if (name == "ni") {
        return *ports[0];
    }
    if (name == "mem_port") {
        return *ports[1];
    }
    if (name == "bar2_mem_port") {
        return *ports[2];
    }
    return ClockedObject::getPort(name, idx);
}

/** 安排下一次处理 */
void
Gw100MemoryProbe::wake(Tick delay)
{
    if (draining) {
        return;
    }
    Tick when = std::max(clockEdge(Cycles(1)), curTick() + delay);
    if (!event.scheduled()) {
        schedule(event, when);
    } else if (when < event.when()) {
        reschedule(event, when);
    }
}

/** 驱动 CRC 输入并启动脚本 */
void
Gw100MemoryProbe::startup()
{
    params().memory_core_domain->perfLevel(0);
    params().memory_ddr_domain->perfLevel(0);
    for (auto *tile : params().memory_tiles) {
        tile->setControl(true, true);
    }
    for (auto *ingress : params().memory_ingresses) {
        ingress->setEnabled(true);
    }
    wake();
}

/** 执行当前脚本步骤 */
void
Gw100MemoryProbe::process()
{
    if (draining || polling) {
        return;
    }
    if (curTick() < pauseUntil) {
        wake(pauseUntil - curTick());
        return;
    }
    if (cursor == operations.size()) {
        if (!outstanding) {
            exitSimLoop("memory probe complete");
        }
        return;
    }
    unsigned kind = operations[cursor];
    unsigned source = operations[cursor + 1];
    Addr address = operations[cursor + 2];
    unsigned size = operations[cursor + 3];
    uint64_t value = operations[cursor + 4];
    if (kind == 15) {
        pauseUntil = curTick() + value;
        cursor += 5;
        wake(value);
        return;
    }
    if (kind == 8) {
        ++generation;
        expected.clear();
        for (auto *ingress : params().memory_ingresses) {
            ingress->invalidate();
        }
        for (auto *tile : params().memory_tiles) {
            tile->resetMemory();
            tile->setControl(true, true);
        }
        cursor += 5;
        wake();
        return;
    }
    if (kind == 6 || kind == 7 || kind == 9 || kind == 14) {
        if (outstanding) {
            return;
        }
        cursor += 5;
        if (kind == 9) {
            exitSimLoop("memory probe checkpoint");
            return;
        }
        if (kind == 14) {
            exitSimLoop("memory probe measurement");
            wake();
            return;
        }
        wake(kind == 7 ? value : 0);
        return;
    }
    panic_if(source >= 3 || !size || size > 256, "Invalid probe request");
    if (outstanding >= maximum || ports[source]->blocked) {
        return;
    }
    if (kind == 5 && outstanding) {
        return;
    }
    bool write = kind == 1 || kind == 4 || kind == 13 || kind == 16;
    auto req = std::make_shared<Request>(address, size, 0, requestor);
    auto *pkt = new Packet(req, write ? MemCmd::WriteReq : MemCmd::ReadReq);
    if (kind == 16) {
        pkt->cmd = MemCmd::SwapReq;
    }
    pkt->allocate();
    if (write) {
        for (unsigned i = 0; i < size; ++i) {
            pkt->getPtr<uint8_t>()[i] = value >> ((i % 8) * 8);
        }
    }
    auto *st = new State{};
    st->kind = kind;
    st->source = source;
    st->address = address;
    st->value = value;
    st->generation = generation;
    pkt->pushSenderState(st);
    ++outstanding;
    if (kind == 5) {
        polling = true;
        if (!pollStarted) {
            pollStarted = curTick();
        }
    } else {
        cursor += 5;
    }
    if (!ports[source]->sendTimingReq(pkt)) {
        ports[source]->blocked = pkt;
        ++rejected;
    }
    if (kind != 5) {
        wake();
    }
}

/** 比较一个响应并更新独立参考 */
void
Gw100MemoryProbe::response(PacketPtr pkt)
{
    auto *st = dynamic_cast<State *>(pkt->popSenderState());
    panic_if(!st, "Probe response has no state");
    bool error = st->kind == 3 || st->kind == 4 || st->kind == 16;
    panic_if(st->kind != 13 && pkt->isError() != error,
             "Probe response mismatch kind %u addr %#x error %u", st->kind,
             st->address, pkt->isError());
    Tick delay = 0;
    if (!pkt->isError()) {
        if ((st->kind == 1 || st->kind == 13) &&
            st->generation == generation) {
            for (unsigned i = 0; i < pkt->getSize(); ++i) {
                expected[st->address + i] = st->value >> ((i % 8) * 8);
            }
        } else if (st->kind == 2) {
            for (unsigned i = 0; i < pkt->getSize(); ++i) {
                panic_if(pkt->getConstPtr<uint8_t>()[i] !=
                             expected[st->address + i],
                         "Probe data mismatch addr %#x", st->address + i);
                ++checked;
            }
        } else if (st->kind == 5) {
            polling = false;
            if (pkt->getUintX(ByteOrder::little) == st->value) {
                cursor += 5;
                pollStarted = 0;
            } else {
                panic_if(curTick() - pollStarted > md::TIMEOUT_PS,
                         "Probe poll timeout addr %#x", st->address);
                delay = 1'000'000;
            }
        } else if (st->kind == 10) {
            uint64_t v = pkt->getUintX(ByteOrder::little);
            panic_if(v != st->value && v != ~st->value,
                     "Probe saw a torn aligned write");
            checked += pkt->getSize();
        } else if (st->kind == 11) {
            panic_if(pkt->getUintX(ByteOrder::little) != st->value,
                     "Probe register mismatch addr %#x", st->address);
        }
    }
    --outstanding;
    delete st;
    delete pkt;
    if (draining && !outstanding) {
        signalDrainDone();
    }
    wake(delay);
}

/** 只在没有在途请求时完成排空 */
DrainState
Gw100MemoryProbe::drain()
{
    draining = true;
    if (event.scheduled()) {
        deschedule(event);
    }
    return outstanding ? DrainState::Draining : DrainState::Drained;
}

/** 恢复 checkpoint 的后续脚本 */
void
Gw100MemoryProbe::drainResume()
{
    draining = false;
    wake();
}

/** 保存已排空的脚本与独立参考 */
void
Gw100MemoryProbe::serialize(CheckpointOut &cp) const
{
    SERIALIZE_SCALAR(cursor);
    SERIALIZE_SCALAR(generation);
    SERIALIZE_SCALAR(checked);
    SERIALIZE_SCALAR(rejected);
    SERIALIZE_SCALAR(pauseUntil);
    std::vector<uint64_t> addresses, values;
    for (const auto &[address, value] : expected) {
        addresses.push_back(address);
        values.push_back(value);
    }
    SERIALIZE_CONTAINER(addresses);
    SERIALIZE_CONTAINER(values);
}

/** 恢复脚本和独立参考 */
void
Gw100MemoryProbe::unserialize(CheckpointIn &cp)
{
    UNSERIALIZE_SCALAR(cursor);
    UNSERIALIZE_SCALAR(generation);
    UNSERIALIZE_SCALAR(checked);
    UNSERIALIZE_SCALAR(rejected);
    UNSERIALIZE_SCALAR(pauseUntil);
    std::vector<uint64_t> addresses, values;
    UNSERIALIZE_CONTAINER(addresses);
    UNSERIALIZE_CONTAINER(values);
    for (unsigned i = 0; i < addresses.size(); ++i) {
        expected[addresses[i]] = values[i];
    }
}

} // namespace gem5::gw100
