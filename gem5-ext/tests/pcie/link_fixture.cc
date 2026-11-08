/**
 * @file  link_fixture.cc
 * @brief 具有有限请求账本和目标后端的独立 PCIe 性能夹具
 *
 * trace 为逐事件 JSONL，外部 Python 检查编码、CRC、顺序和资源。
 *
 * @see spec-pcie
 */

#include <algorithm>
#include <array>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>

#include "src/gw100/pcie/link/link_model.hh"

using namespace gem5::gw100::pcie;
namespace defs = gem5::gw100::pciedefs;

/** 命令行给定的确定性工作负载。 */
struct Workload
{
    /** 目标速率。 */
    unsigned generation = 4;
    /** 目标宽度。 */
    unsigned width = 4;
    /** write、read 或 EP 同时发起 MRd/MWr 的 dma。 */
    std::string operation = "read";
    /** 两方向同时发起业务。 */
    bool duplex = false;
    /** MPS，单位 B。 */
    unsigned mps = 256;
    /** MRRS，单位 B。 */
    unsigned mrrs = 512;
    /** RCB，单位 B。 */
    unsigned rcb = 64;
    /** 完成载荷上限，单位 B。 */
    unsigned completion = 256;
    /** 每个完整目标请求的服务等待，单位 ps。 */
    uint64_t service = 2500000;
    /** 数据期望种子。 */
    unsigned seed = 1;
    /** 原始事件保存路径。 */
    std::string trace;
};

/** 有限槽的目标服务与独立请求账本。 */
class Fixture : public LinkClockIf, public LinkPeerIf
{
  public:
    /** 由工作负载建立两端。 */
    explicit Fixture(const Workload &workload)
        : workload(workload), link(*this, *this, configuration(workload))
    {
        if (workload.operation != "write" &&
            workload.completion < workload.rcb) {
            throw std::invalid_argument("completion payload below RCB");
        }
        if (!workload.trace.empty()) {
            trace.open(workload.trace);
            if (!trace) {
                throw std::runtime_error("cannot create trace");
            }
        }
        link.start();
    }
    /** 从工作负载建立数字链路配置。 */
    static LinkConfig
    configuration(const Workload &workload)
    {
        LinkConfig config;
        config.generation = workload.generation;
        config.width = workload.width;
        return config;
    }
    /** 当前时刻，单位 ps。 */
    uint64_t
    now() const override
    {
        return time;
    }
    /** 最早协议事件唤醒。 */
    void
    wakeAt(uint64_t when) override
    {
        due = std::min(due, when);
    }
    /** 目标只有有限后端能保留操作时才归还 RX 信用。 */
    bool
    receive(unsigned side, const Tlp &tlp,
            std::function<void()> release) override
    {
        if (tlp.kind == TlpKind::Completion) {
            auto found = pending[side].find(tlp.tag);
            if (found == pending[side].end()) {
                throw std::runtime_error("unexpected completion tag");
            }
            auto &request = found->second;
            unsigned remaining =
                request.request.requestBytes() - request.offset;
            uint64_t address = request.request.address +
                               request.request.firstOffset() + request.offset;
            unsigned prefix = address & 3;
            unsigned returned =
                std::min<unsigned>(remaining, tlp.payload.size() - prefix);
            if (tlp.requester != request.request.requester ||
                tlp.status != CompletionStatus::Success ||
                tlp.byteCount != remaining ||
                tlp.lowerAddress != (address & 127) || !returned) {
                throw std::runtime_error("completion fields");
            }
            for (unsigned byte = 0; byte < returned; ++byte) {
                if (tlp.payload[prefix + byte] != pattern(address + byte)) {
                    throw std::runtime_error("completion data");
                }
            }
            request.offset += returned;
            release();
            if (request.offset == request.request.requestBytes()) {
                account(workload.operation == "dma" ? 0 : side,
                        request.offset);
                link.noteComplete(side, request.request.cookie);
                reserved[side] -= request.request.length * 4;
                pending[side].erase(found);
            }
            return true;
        }
        if (tlp.traffic() == Traffic::NonPosted) {
            auto [entry, inserted] =
                postedDependencies[side].try_emplace(tlp.cookie);
            if (inserted) {
                for (const auto &job : jobs[side]) {
                    if (job.request.traffic() == Traffic::Posted) {
                        entry->second.push_back(job.request.cookie);
                    }
                }
            }
            for (auto dependency : entry->second) {
                if (std::any_of(jobs[side].begin(), jobs[side].end(),
                                [dependency](const Job &job) {
                                    return job.request.cookie == dependency;
                                })) {
                    return false;
                }
            }
        }
        if (jobs[side].size() == defs::BACKEND_SLOTS) {
            return false;
        }
        if (side == 0 && tlp.traffic() == Traffic::NonPosted &&
            std::count_if(
                jobs[side].begin(), jobs[side].end(),
                [](const Job &job) {
                    return job.request.traffic() == Traffic::NonPosted;
                }) >= static_cast<long>(defs::BACKEND_SLOTS -
                                        defs::RC_POSTED_RESERVED_SLOTS)) {
            return false;
        }
        uint64_t service = workload.operation == "dma" && side == 0 &&
                                   tlp.kind == TlpKind::MemoryWrite
                               ? 250000
                               : workload.service;
        jobs[side].push_back({tlp, 0, time + service});
        postedDependencies[side].erase(tlp.cookie);
        release();
        return true;
    }
    /** 有限待发队列有空位时让发生器继续提交。 */
    void
    transmitReady(unsigned) override
    {
        fill();
    }
    /** 夹具的请求账本与目标服务全部完成。 */
    bool
    quiescent() const override
    {
        return stopped && pending[0].empty() && pending[1].empty() &&
               jobs[0].empty() && jobs[1].empty();
    }
    /** 正常基准不允许任何协议错误。 */
    void
    error(unsigned, LinkError, const std::vector<uint8_t> &) override
    {
        throw std::runtime_error("unexpected protocol error in benchmark");
    }
    /** 保存线上原始字节、精确相位及实体资源快照。 */
    void
    observe(const LinkTrace &event) override
    {
        if (event.point == LinkPoint::Visible && event.cookie == probeCookie) {
            probeVisible = event.tick;
        }
        if (!trace) {
            return;
        }
        trace << "{\"point\":\"" << linkPointName(event.point)
              << "\",\"tick\":" << event.tick << ",\"side\":" << event.side
              << ",\"cookie\":" << event.cookie << ",\"value\":" << event.value
              << ",\"replay\":" << event.replay
              << ",\"generation\":" << link.generation()
              << ",\"width\":" << link.width()
              << ",\"begin_half\":" << event.frame.beginHalf
              << ",\"end_half\":" << event.frame.endHalf
              << ",\"encoding_half\":" << event.frame.encodingHalf
              << ",\"padding_half\":" << event.frame.paddingHalf
              << ",\"ordered_half\":" << event.frame.orderedHalf
              << ",\"bytes\":\"" << hex(event.bytes) << "\",\"framing\":\""
              << hex(event.framing) << "\"";
        for (unsigned side = 0; side < 2; ++side) {
            trace << ",\"backend" << side << "\":" << jobs[side].size()
                  << ",\"backend_np" << side << "\":"
                  << std::count_if(jobs[side].begin(), jobs[side].end(),
                                   [](const Job &job) {
                                       return job.request.traffic() ==
                                              Traffic::NonPosted;
                                   });
            auto resources = link.resources(side);
            trace << ",\"resources" << side << "\":[";
            for (auto array : {resources.txHeaders, resources.txBytes,
                               resources.rxHeaders, resources.rxBytes}) {
                for (auto count : array) {
                    trace << count << ',';
                }
            }
            trace << resources.replayBytes << ',' << resources.replayHeaders
                  << ']';
        }
        trace << "}\n";
    }
    /** 完成训练、固定预热、测量与最终排空。 */
    void
    run()
    {
        pumpUntil([this] {
            return link.state() == LinkState::L0 && link.active(0) &&
                   link.active(1) && link.generation() == workload.generation;
        });
        if (!link.configure(0, workload.mps, false) ||
            !link.configure(1, workload.mps, false)) {
            throw std::runtime_error("fixture MPS configuration");
        }
        stopped = true;
        link.drain();
        pumpUntil([this] { return link.isDrained(); });
        probeWire = link.snapshot().ports[0].wire;
        link.resume();
        probeStart = time;
        std::vector<uint8_t> data(64);
        for (unsigned byte = 0; byte < data.size(); ++byte) {
            data[byte] = pattern(0x1000 + byte);
        }
        auto probe = Tlp::request(TlpKind::MemoryWrite, 0x1000, data.size(), 0,
                                  0, data.data());
        if (!link.submit(0, probe)) {
            throw std::runtime_error("isolated latency probe admission");
        }
        probeCookie = probe.cookie;
        pumpUntil([this] { return probeVisible != 0; });
        link.drain();
        pumpUntil([this] { return link.isDrained(); });
        link.resume();
        stopped = false;
        measureStart = time + defs::BENCH_WARMUP_PS;
        measureEnd = measureStart + defs::BENCH_WINDOW_PS;
        running = true;
        fill();
        pumpUntil([this] { return time >= measureEnd; });
        stopped = true;
        link.drain();
        pumpUntil([this] { return link.isDrained(); });
        std::cout << "{\"generation\":" << workload.generation
                  << ",\"width\":" << workload.width << ",\"operation\":\""
                  << workload.operation << "\",\"duplex\":" << workload.duplex
                  << ",\"mps\":" << workload.mps
                  << ",\"mrrs\":" << workload.mrrs
                  << ",\"rcb\":" << workload.rcb
                  << ",\"completion\":" << workload.completion
                  << ",\"service_ps\":" << workload.service
                  << ",\"write_service_ps\":"
                  << (workload.operation == "dma" ? 250000 : workload.service)
                  << ",\"seed\":" << workload.seed
                  << ",\"latency_probe\":{\"cookie\":" << probeCookie
                  << ",\"start_ps\":" << probeStart
                  << ",\"visible_ps\":" << probeVisible
                  << ",\"origin_half\":" << probeWire.dataOriginHalf
                  << ",\"symbols\":" << probeWire.dataSymbols << '}'
                  << ",\"start_ps\":" << measureStart
                  << ",\"window_ps\":" << defs::BENCH_WINDOW_PS
                  << ",\"bytes\":[" << measured[0] << ',' << measured[1]
                  << "],\"end_ps\":" << time << "}\n";
    }

  private:
    /** 请求者已预留的完整读请求。 */
    struct Pending
    {
        /** 线上请求。 */
        Tlp request;
        /** 已收到的实际请求字节数。 */
        unsigned offset = 0;
    };
    /** 一个有限目标执行槽。 */
    struct Job
    {
        /** 收到的原始请求。 */
        Tlp request;
        /** 已返回的读数据字节数。 */
        unsigned offset = 0;
        /** 目标等待结束，单位 ps。 */
        uint64_t ready = 0;
    };
    /** 确定性数据字节，Python 用同一公开定义独立生成期望。 */
    uint8_t
    pattern(uint64_t address) const
    {
        return (address ^ (address >> 8) ^ workload.seed) & 255;
    }
    /** 原始线上字节转为十六进制，无格式化后的寄存器替代。 */
    static std::string
    hex(const std::vector<uint8_t> &bytes)
    {
        std::ostringstream output;
        output << std::hex << std::setfill('0');
        for (auto byte : bytes) {
            output << std::setw(2) << unsigned(byte);
        }
        return output.str();
    }
    /** 累计实际服务完成或全部 completion 到达的有效载荷。 */
    void
    account(unsigned source, unsigned bytes)
    {
        if (time >= measureStart && time < measureEnd) {
            measured[source] += bytes;
        }
    }
    /** 按有限 tag、完成数据和核心待发队列生成工作。 */
    void
    fill()
    {
        if (!running || stopped || filling || time >= measureEnd) {
            return;
        }
        filling = true;
        for (unsigned flow = 0; flow < (workload.duplex ? 2u : 1u); ++flow) {
            unsigned side = workload.operation == "dma" ? 1 : flow;
            bool read = workload.operation == "read" ||
                        (workload.operation == "dma" && flow == 0);
            while (true) {
                unsigned bytes = read ? workload.mrrs : workload.mps;
                unsigned tag = 0;
                if (read) {
                    if (pending[side].size() == defs::MAX_OUTSTANDING_READS ||
                        reserved[side] + bytes > defs::COMPLETION_DATA_BYTES) {
                        break;
                    }
                    while (pending[side].count(tag)) {
                        ++tag;
                    }
                }
                uint64_t address = 0x100000 + sequence[flow] * bytes;
                std::vector<uint8_t> data(bytes);
                for (unsigned i = 0; i < bytes; ++i) {
                    data[i] = pattern(address + i);
                }
                auto tlp = Tlp::request(read ? TlpKind::MemoryRead
                                             : TlpKind::MemoryWrite,
                                        address, bytes, side ? 0x100 : 0, tag,
                                        read ? nullptr : data.data());
                if (!link.submit(side, tlp)) {
                    break;
                }
                if (read) {
                    pending[side].emplace(tag, Pending{tlp, 0});
                    reserved[side] += bytes;
                }
                ++sequence[flow];
            }
        }
        filling = false;
    }
    /** 将到期目标服务转为可见写入或受 MPS/RCB 限制的完成。 */
    void
    serve()
    {
        for (unsigned side = 0; side < 2; ++side) {
            auto &queue = jobs[side];
            for (auto entry = queue.begin(); entry != queue.end();) {
                if (entry->ready > time) {
                    ++entry;
                    continue;
                }
                auto &request = entry->request;
                uint64_t address = request.address + request.firstOffset();
                if (request.kind == TlpKind::MemoryWrite) {
                    for (unsigned i = 0; i < request.length * 4; ++i) {
                        if (request.byteEnabled(i) &&
                            request.payload[i] !=
                                pattern(request.address + i)) {
                            throw std::runtime_error("posted data");
                        }
                    }
                    link.noteVisible(side, request.cookie);
                    account(workload.operation == "dma" ? 1 : side ^ 1,
                            request.requestBytes());
                    entry = queue.erase(entry);
                    link.retryReceive();
                    continue;
                }
                unsigned remaining = request.requestBytes() - entry->offset;
                unsigned bytes =
                    std::min({remaining, workload.completion, workload.mps});
                if (bytes < remaining) {
                    bytes -= (address + entry->offset + bytes) % workload.rcb;
                }
                if (!bytes) {
                    throw std::runtime_error("invalid completion split");
                }
                std::vector<uint8_t> data(bytes);
                for (unsigned i = 0; i < bytes; ++i) {
                    data[i] = pattern(address + entry->offset + i);
                }
                auto completion = Tlp::completion(request, side ? 0x100 : 0,
                                                  entry->offset, data);
                if (!link.submit(side, completion)) {
                    ++entry;
                    continue;
                }
                entry->offset += bytes;
                if (entry->offset == request.requestBytes()) {
                    entry = queue.erase(entry);
                    link.retryReceive();
                }
            }
        }
    }
    /** 只在真实协议、目标服务或测量边界上推进。 */
    void
    pumpUntil(const std::function<bool()> &done)
    {
        for (unsigned steps = 0; !done(); ++steps) {
            if (steps == 5000000) {
                throw std::runtime_error("fixture event bound");
            }
            uint64_t next = due;
            if (running && !stopped) {
                next = std::min(next, measureEnd);
            }
            for (const auto &queue : jobs) {
                for (const auto &entry : queue) {
                    if (entry.ready > time) {
                        next = std::min(next, entry.ready);
                    }
                }
            }
            if (next == UINT64_MAX || next < time) {
                throw std::runtime_error("fixture stalled");
            }
            time = next;
            due = UINT64_MAX;
            link.tick();
            serve();
            fill();
            // 同一完成队列有空位时继续拆包，下一时钟沿再提交。
            if (!jobs[0].empty() || !jobs[1].empty()) {
                due = std::min(due, time + defs::CONTROLLER_PERIOD_PS);
            }
        }
    }
    /** 本轮工作负载。 */
    Workload workload;
    /** 被测双向链路。 */
    LinkModel link;
    /** 当前夹具时刻，单位 ps。 */
    uint64_t time = 0;
    /** 下一唤醒，单位 ps。 */
    uint64_t due = UINT64_MAX;
    /** 是否已开始生成业务。 */
    bool running = false;
    /** 是否已结束业务产生。 */
    bool stopped = false;
    /** 阻止提交回调重入发生器。 */
    bool filling = false;
    /** 固定测量窗口首时刻，单位 ps。 */
    uint64_t measureStart = 0;
    /** 固定测量窗口末时刻，单位 ps。 */
    uint64_t measureEnd = 0;
    /** 各请求源累计有效载荷。 */
    std::array<uint64_t, 2> measured{};
    /** 各请求源下一数据地址序号。 */
    std::array<uint64_t, 2> sequence{};
    /** 各请求源预留完成数据字节。 */
    std::array<unsigned, 2> reserved{};
    /** 各请求源以真实 tag 索引的有限账本。 */
    std::array<std::map<unsigned, Pending>, 2> pending;
    /** 每端最多六十四个后端执行槽。 */
    std::array<std::deque<Job>, 2> jobs;
    /** 每个 RX NP 槽首次到达时已有的 posted 目标依赖。 */
    std::array<std::map<uint64_t, std::vector<uint64_t>>, 2>
        postedDependencies;
    /** 可选原始 JSONL 输出。 */
    std::ofstream trace;
    /** 独立孤立写延迟的初始串行相位。 */
    Serializer::State probeWire;
    /** 孤立写的观测编号。 */
    uint64_t probeCookie = 0;
    /** 孤立写的本地接受时刻，单位 ps。 */
    uint64_t probeStart = 0;
    /** 孤立写的远端可见时刻，单位 ps。 */
    uint64_t probeVisible = 0;
};

/** 执行一个明确配置的夹具，错误直接使进程失败。 */
int
main(int argc, char **argv)
{
    try {
        if (argc != 11) {
            throw std::invalid_argument("gen width read/write duplex mps mrrs "
                                        "rcb cpl service_ps trace");
        }
        Workload workload;
        workload.generation = std::stoul(argv[1]);
        workload.width = std::stoul(argv[2]);
        workload.operation = argv[3];
        workload.duplex = std::stoul(argv[4]);
        workload.mps = std::stoul(argv[5]);
        workload.mrrs = std::stoul(argv[6]);
        workload.rcb = std::stoul(argv[7]);
        workload.completion = std::stoul(argv[8]);
        workload.service = std::stoull(argv[9]);
        workload.trace = argv[10];
        if ((workload.operation != "read" && workload.operation != "write" &&
             workload.operation != "dma") ||
            (workload.operation == "dma" && !workload.duplex) ||
            (workload.mps != 128 && workload.mps != 256) ||
            (workload.mrrs != 128 && workload.mrrs != 256 &&
             workload.mrrs != 512) ||
            (workload.rcb != 64 && workload.rcb != 128) ||
            (workload.completion != 64 && workload.completion != 128 &&
             workload.completion != 256)) {
            throw std::invalid_argument("fixture workload");
        }
        Fixture fixture(workload);
        fixture.run();
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
