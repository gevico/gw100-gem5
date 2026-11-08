/**
 * @file  gw100_sram.cc
 * @brief 共享 SRAM 的 gem5 端口实现
 * @see spec-sram
 */
#include "src/gw100/sram/gw100_sram.hh"

#include <algorithm>

#include "base/logging.hh"
#include "mem/packet.hh"
#include "mem/packet_access.hh"
#include "sim/serialize.hh"

namespace gem5::gw100
{
namespace
{
/** 将 SimObject 参数完整转换为状态机配置。 */
SramModel::Config
configuration(const Gw100SramParams &p)
{
    SramModel::Config cfg;
    cfg.size = p.size;
    cfg.banks = p.banks;
    cfg.wordBytes = p.word_bytes;
    cfg.sources = p.sources;
    cfg.slots = p.slots;
    cfg.readCycles = p.read_cycles;
    cfg.writeCycles = p.write_cycles;
    cfg.interval = p.interval_cycles;
    cfg.maskBytes = p.mask_bytes;
    cfg.interleaved = p.interleaved;
    cfg.splitPorts = p.split_ports;
    cfg.ecc = p.ecc;
    return cfg;
}
} // namespace

/** 构造实例、有限入口与可选的观测和故障夹具。 */
Gw100Sram::Gw100Sram(const Params &p)
    : ClockedObject(p),
      core(configuration(p)),
      metrics(*this),
      start(p.base),
      edge([this] { process(); }, name() + ".edge", false,
           Event::CPU_Tick_Pri + 10),
      faultStage(p.fault_stage),
      faultOffset(p.fault_offset),
      faultMask(p.fault_mask),
      faultSource(p.fault_source),
      faultAnyWord(p.fault_any_word)
{
    fatal_if(start > MaxAddr - size(), "%s: SRAM 地址范围溢出", name());
    fatal_if(faultStage != "commit" && faultStage != "issue_read" &&
                 faultStage != "rmw" && faultStage != "fetch",
             "%s: SRAM 注入点错误", name());
    fatal_if(faultSource >= p.sources, "%s: SRAM 注入来源错误", name());
    for (unsigned source = 0; source < p.sources; ++source) {
        ports.push_back(std::make_unique<SramPort>(
            name() + ".ports[" + std::to_string(source) + "]", *this, source));
    }
    ingress.resize(p.sources);
    inFlight.resize(p.sources);
    lastAccept.assign(p.sources, MaxTick);
    if (!p.trace_file.empty()) {
        traceFile.open(p.trace_file);
        fatal_if(!traceFile, "%s: SRAM trace 无法写入", name());
    }
    if (!p.trace_file.empty() || faultMask) {
        core.trace = [this](const std::string &event, unsigned source,
                            uint64_t id, uint64_t address, uint64_t cycle) {
            if (faultMask && !faultInjected && source == faultSource &&
                event == faultStage &&
                (faultAnyWord || address == faultOffset)) {
                faultInjected = true;
                for (unsigned bit = 0; bit < 64; ++bit) {
                    if ((faultMask >> bit) & 1) {
                        core.inject(address, bit);
                    }
                }
            }
            if (!traceFile.is_open()) {
                return;
            }
            traceFile << "{\"event\":\"" << event << "\",\"source\":" << source
                      << ",\"id\":" << id << ",\"address\":" << address
                      << ",\"cycle\":" << cycle << ",\"tick\":" << curTick()
                      << "}\n";
            traceFile.flush();
        };
    }
}

/** 注册统计量及其字节、周期或计数单位。 */
Gw100Sram::Metrics::Metrics(Gw100Sram &owner)
    : statistics::Group(&owner),
      ADD_STAT(accepted, statistics::units::Count::get(), "阵列准入事务"),
      ADD_STAT(completed, statistics::units::Count::get(), "响应就绪事务"),
      ADD_STAT(rejected, statistics::units::Count::get(), "错误完成事务"),
      ADD_STAT(reads, statistics::units::Count::get(), "宏读命令"),
      ADD_STAT(writes, statistics::units::Count::get(), "宏写命令"),
      ADD_STAT(rmw, statistics::units::Count::get(), "读改写操作"),
      ADD_STAT(ce, statistics::units::Count::get(), "纠正的码字"),
      ADD_STAT(ue, statistics::units::Count::get(), "不可纠正的码字"),
      ADD_STAT(readBytes, statistics::units::Byte::get(), "有效读数据"),
      ADD_STAT(writeBytes, statistics::units::Byte::get(), "提交的有效写数据"),
      ADD_STAT(initWrites, statistics::units::Count::get(), "清零提交宏字"),
      ADD_STAT(bankWait, statistics::units::Cycle::get(),
               "累计 bank 仲裁等待"),
      ADD_STAT(maxBankWait, statistics::units::Cycle::get(),
               "最长 bank 仲裁等待"),
      ADD_STAT(queueWait, statistics::units::Cycle::get(),
               "操作进入可执行队首等待"),
      ADD_STAT(dependencyWait, statistics::units::Cycle::get(),
               "宏字依赖等待"),
      ADD_STAT(responseWait, statistics::units::Cycle::get(), "响应接受等待")
{}

/** 将状态机计数同步到 gem5 统计。 */
void
Gw100Sram::preDumpStats()
{
    ClockedObject::preDumpStats();
    const auto &s = core.stats();
    metrics.accepted = s.accepted;
    metrics.completed = s.completed;
    metrics.rejected = s.rejected;
    metrics.reads = s.reads;
    metrics.writes = s.writes;
    metrics.rmw = s.rmw;
    metrics.ce = s.ce;
    metrics.ue = s.ue;
    metrics.readBytes = s.readBytes;
    metrics.writeBytes = s.writeBytes;
    metrics.initWrites = s.initWrites;
    metrics.bankWait = s.bankWait;
    metrics.maxBankWait = s.maxBankWait;
    metrics.queueWait = s.queueWait;
    metrics.dependencyWait = s.dependencyWait;
    metrics.responseWait = s.responseWait;
}

/** 返回硬件绑定的来源端口。 */
Port &
Gw100Sram::getPort(const std::string &name, PortID index)
{
    if (name == "ports") {
        fatal_if(index < 0 || unsigned(index) >= ports.size(),
                 "%s: SRAM 来源越界", this->name());
        return *ports[index];
    }
    return ClockedObject::getPort(name, index);
}
/** 向连接入口公布地址范围。 */
void
Gw100Sram::init()
{
    ClockedObject::init();
    for (auto &port : ports) {
        if (port->isConnected()) {
            port->sendRangeChange();
        }
    }
}
/** 检查全局非空请求范围，包含溢出检查。 */
bool
Gw100Sram::contains(Addr address, unsigned length) const
{
    return length && address >= start && address - start < size() &&
           length <= size() - (address - start);
}
/** 在当前或下一合法边沿安排服务事件。 */
void
Gw100Sram::arm()
{
    if (edge.scheduled() || core.isPaused()) {
        return;
    }
    Tick next = clockEdge();
    if (lastEdge != MaxTick && next <= lastEdge) {
        next = clockEdge(Cycles(1));
    }
    schedule(edge, next);
}
/** 预留完整事务额度并保存带到达时间的请求。 */
bool
Gw100Sram::receive(unsigned source, PacketPtr packet)
{
    if (draining || inFlight[source] == core.config().slots ||
        lastAccept[source] == clockEdge()) {
        ports[source]->retry = true;
        arm();
        return false;
    }
    ++inFlight[source];
    lastAccept[source] = clockEdge();
    ingress[source].push_back(
        {packet, curTick(),
         curTick() + packet->headerDelay + packet->payloadDelay,
         core.epoch()});
    packet->headerDelay = packet->payloadDelay = 0;
    arm();
    return true;
}

/** 执行到达准入、阵列边沿、返回和 retry。 */
void
Gw100Sram::process()
{
    lastEdge = curTick();
    core.step([this] {
        for (unsigned source = 0; source < ports.size(); ++source) {
            auto &queue = ingress[source];
            if (queue.empty() || queue.front().arrival > curTick()) {
                continue;
            }
            auto pending = queue.front();
            PacketPtr packet = pending.packet;
            SramModel::Request request;
            bool supported = (packet->isRead() || packet->isWrite()) &&
                             !packet->isLLSC() && !packet->isAtomicOp();
            request.address =
                !pending.cancelled && supported &&
                        contains(packet->getAddr(), packet->getSize())
                    ? packet->getAddr() - start
                    : size();
            request.size = packet->getSize();
            request.write = packet->isWrite();
            request.instruction = packet->req->isInstFetch();
            request.mask = request.size >= 64
                               ? ~uint64_t(0)
                               : (uint64_t(1) << request.size) - 1;
            if (request.write && supported && request.size <= 64) {
                std::copy_n(packet->getConstPtr<uint8_t>(), request.size,
                            request.data.begin());
                const auto &enable = packet->req->getByteEnable();
                if (!enable.empty()) {
                    for (unsigned i = 0; i < request.size; ++i) {
                        if (!enable[i]) {
                            request.mask &= ~(uint64_t(1) << i);
                        }
                    }
                }
            }
            uint64_t id;
            if (core.submit(source, request, id,
                            pending.cancelled ? pending.epoch : UINT64_MAX)) {
                if (traceFile.is_open()) {
                    traceFile
                        << "{\"event\":\"transport\",\"source\":" << source
                        << ",\"id\":" << id
                        << ",\"address\":" << request.address
                        << ",\"tick\":" << curTick()
                        << ",\"upstream_accept\":" << pending.accepted
                        << ",\"arrival\":" << pending.arrival << "}\n";
                    traceFile.flush();
                }
                packets.emplace(id, PacketState{packet});
                queue.pop_front();
            }
        }
    });
    for (unsigned source = 0; source < ports.size(); ++source) {
        auto &port = *ports[source];
        auto result = core.response(source);
        if (result && !port.blocked) {
            uint64_t id = result->id;
            auto &context = packets.at(id);
            PacketPtr packet = context.packet;
            bool response = packet->needsResponse() || context.prepared;
            if (!context.prepared) {
                if (packet->isRead() &&
                    result->status == SramModel::Status::Ok) {
                    packet->setData(result->data.data());
                }
                if (response) {
                    packet->makeResponse();
                    if (result->status != SramModel::Status::Ok) {
                        packet->setBadAddress();
                    }
                }
                context.prepared = true;
            }
            if (!response || port.sendTimingResp(packet)) {
                if (!response) {
                    delete packet;
                }
                core.acceptResponse(source);
                packets.erase(id);
                --inFlight[source];
            } else {
                port.blocked = true;
            }
        }
        if (port.retry && !draining &&
            inFlight[source] < core.config().slots &&
            lastAccept[source] < clockEdge(Cycles(1))) {
            // retry 本身不转移所有权，接收端仍执行完整的有限准入。
            port.retry = false;
            port.sendRetryReq();
        }
    }
    if (initializePending && core.drained()) {
        initializePending = false;
        core.initialize();
    }
    bool queued = false;
    for (auto count : inFlight) {
        queued |= count != 0;
    }
    if (draining && core.drained() && !queued &&
        drainState() == DrainState::Draining) {
        signalDrainDone();
    }
    if (core.busy() || queued) {
        arm();
    }
}
/** 收尾旧代次后开始端口清零，完成由 Ready 状态通知。 */
void
Gw100Sram::initialize()
{
    if (core.drained()) {
        core.initialize();
    } else {
        initializePending = true;
    }
    arm();
}
/** 取消未提交操作并标记入口中的旧代次请求。 */
void
Gw100Sram::resetMemory(bool preserve)
{
    if (edge.scheduled()) {
        deschedule(edge);
    }
    core.reset(preserve);
    initializePending = false;
    for (auto &queue : ingress) {
        for (auto &pending : queue) {
            pending.cancelled = true;
            pending.arrival = curTick();
        }
    }
    arm();
}
/** 停止 SRAM 有效边沿并保存剩余流水周期。 */
void
Gw100Sram::pauseMemory()
{
    if (core.trace) {
        core.trace("pause", 0, 0, 0, core.now());
    }
    core.pause();
    if (edge.scheduled()) {
        deschedule(edge);
    }
}
/** 在当前时钟域中恢复有效边沿。 */
void
Gw100Sram::resumeMemory()
{
    core.resume();
    if (core.trace) {
        core.trace("resume", 0, 0, 0, core.now());
    }
    arm();
}
/** 请求 gem5 排空并等待全部已接受请求收尾。 */
DrainState
Gw100Sram::drain()
{
    draining = true;
    return drainedMemory() ? DrainState::Drained : DrainState::Draining;
}

/** 检查来源的入口、阵列和返回额度是否全部释放。 */
bool
Gw100Sram::sourceDrained(unsigned source) const
{ return inFlight.at(source) == 0 && core.sourceDrained(source); }

/** 检查整个实例的入口、阵列与响应是否已排空。 */
bool
Gw100Sram::drainedMemory() const
{
    return core.drained() &&
           std::all_of(inFlight.begin(), inFlight.end(),
                       [](unsigned count) { return count == 0; });
}

/** 整体排空后进入保持，返回是否成功。 */
bool
Gw100Sram::enterRetention()
{ return drainedMemory() && core.enterRetention(); }

/** 显式退出保持并恢复时钟服务。 */
void
Gw100Sram::leaveRetention()
{
    core.leaveRetention();
    arm();
}
/** 恢复排空前的正常准入。 */
void
Gw100Sram::drainResume()
{
    draining = false;
    arm();
}
/** 写入稳定态组件快照与夹具状态。 */
void
Gw100Sram::serialize(CheckpointOut &cp) const
{
    SERIALIZE_SCALAR(faultInjected);
    std::ofstream out(CheckpointIn::dir() + name() + ".sram",
                      std::ios::binary);
    core.save(out);
}
/** 恢复经组织指纹校验的组件快照。 */
void
Gw100Sram::unserialize(CheckpointIn &cp)
{
    UNSERIALIZE_SCALAR(faultInjected);
    if (edge.scheduled()) {
        deschedule(edge);
    }
    core.reset(false);
    std::ifstream in(cp.getCptDir() + "/" + name() + ".sram",
                     std::ios::binary);
    core.restore(in);
}
/** 对排空存储执行功能访问并转换为 gem5 响应。 */
void
Gw100Sram::functional(PacketPtr packet)
{
    fatal_if(!contains(packet->getAddr(), packet->getSize()),
             "%s: 功能访问越界", name());
    uint64_t offset = packet->getAddr() - start;
    if (packet->isWrite()) {
        core.functionalWrite(offset, packet->getConstPtr<uint8_t>(),
                             packet->getSize());
    } else {
        core.functionalRead(offset, packet->getPtr<uint8_t>(),
                            packet->getSize());
    }
    if (packet->needsResponse()) {
        packet->makeResponse();
    }
}
/** 绑定所属实例及不可伪造的来源号。 */
Gw100Sram::SramPort::SramPort(const std::string &name, Gw100Sram &owner,
                              unsigned source)
    : ResponsePort(name, source), owner(owner), source(source)
{}
/** 在有限资源允许时接受 timing 请求。 */
bool
Gw100Sram::SramPort::recvTimingReq(PacketPtr packet)
{ return owner.receive(source, packet); }
/** 执行显式兼容原子访问并返回参考延迟。 */
Tick
Gw100Sram::SramPort::recvAtomic(PacketPtr packet)
{
    owner.functional(packet);
    return owner.clockPeriod() * (packet->isWrite()
                                      ? owner.core.config().writeCycles
                                      : owner.core.config().readCycles);
}
/** 对排空的存储执行调试或加载访问。 */
void
Gw100Sram::SramPort::recvFunctional(PacketPtr packet)
{ owner.functional(packet); }
/** 上游释放响应阻塞后重新安排返回。 */
void
Gw100Sram::SramPort::recvRespRetry()
{
    blocked = false;
    owner.arm();
}
/** 公布该 SRAM 实例的数据窗口。 */
AddrRangeList
Gw100Sram::SramPort::getAddrRanges() const
{ return {RangeSize(owner.start, owner.size())}; }
} // namespace gem5::gw100
