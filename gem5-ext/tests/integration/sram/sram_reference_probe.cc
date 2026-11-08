/**
 * @file  sram_reference_probe.cc
 * @brief 独立参考工具使用的 SRAM 命令驱动器
 * @see spec-sram
 */
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include "src/gw100/sram/sram_model.hh"

using gem5::gw100::SramModel;

namespace
{
/** 完整执行参考档案的逐拍初始化。 */
void
ready(SramModel &model)
{
    model.initialize();
    while (model.state() != SramModel::State::Ready) {
        model.step();
    }
}
/** 输出并接受来源的一个完成记录。 */
void
completion(SramModel &model, unsigned source)
{
    auto result = model.response(source);
    if (!result) {
        std::cout << "{\"empty\":true}\n";
        return;
    }
    std::ostringstream data;
    for (uint8_t value : result->data) {
        data << std::hex << std::setw(2) << std::setfill('0')
             << unsigned(value);
    }
    std::cout << "{\"result\":" << result->id
              << ",\"status\":" << unsigned(result->status)
              << ",\"committed\":" << result->committed
              << ",\"accepted\":" << result->accepted
              << ",\"ready\":" << result->ready << ",\"data\":\"" << data.str()
              << "\"}\n";
    model.acceptResponse(source);
}
/** 按固定窗口驱动有界饱和流量并输出原始计数。 */
void
benchmark(SramModel &model, const std::string &kind, uint64_t period)
{
    const auto &cfg = model.config();
    uint64_t warm = 10000000 / period, window = 100000000 / period;
    uint64_t beginBytes = 0, endBytes = 0, peak = 0, maxWait = 0;
    std::vector<uint64_t> sequence(cfg.sources);
    std::map<uint64_t, SramModel::Request> expected;
    auto collect = [&](unsigned source) {
        const auto *result = model.response(source);
        if (!result) {
            return;
        }
        const auto &request = expected.at(result->id);
        if (result->status != SramModel::Status::Ok ||
            (request.write && result->committed != request.mask)) {
            throw std::runtime_error("benchmark 数据完成错误");
        }
        if (!request.write) {
            for (unsigned i = 0; i < request.size; ++i) {
                if (result->data[i]) {
                    throw std::runtime_error("benchmark 读取数据错误");
                }
            }
        }
        expected.erase(result->id);
        model.acceptResponse(source);
    };
    for (uint64_t tick = 0; tick < warm + window; ++tick) {
        for (unsigned s = 0; s < cfg.sources; ++s) {
            collect(s);
            SramModel::Request request;
            request.size = kind == "partial" ? 1 : cfg.wordBytes;
            request.mask = request.size == 64
                               ? ~uint64_t(0)
                               : (uint64_t(1) << request.size) - 1;
            request.write = kind != "read" && (kind != "mixed" || s % 2);
            uint64_t word =
                (sequence[s] * cfg.sources + s) % (cfg.size / cfg.wordBytes);
            unsigned bank = word % cfg.banks;
            uint64_t row = word / cfg.banks;
            if (kind == "hotspot" || kind == "mixed" || kind == "partial") {
                bank = 0;
                row = s * 64 + sequence[s] % 64;
            }
            request.address =
                cfg.interleaved
                    ? (row * cfg.banks + bank) * cfg.wordBytes
                    : bank * (cfg.size / cfg.banks) + row * cfg.wordBytes;
            request.data.fill(uint8_t(s + 1));
            uint64_t id;
            if (model.submit(s, request, id)) {
                expected.emplace(id, request);
                ++sequence[s];
            }
        }
        model.step();
        auto bytes = model.stats().readBytes + model.stats().writeBytes;
        if (tick + 1 == warm) {
            beginBytes = bytes;
        }
        if (tick + 1 == warm + window) {
            endBytes = bytes;
        }
        peak = std::max<uint64_t>(peak, expected.size());
        maxWait = model.stats().maxBankWait;
    }
    for (unsigned count = 0; !model.drained() && count < 10000; ++count) {
        for (unsigned s = 0; s < cfg.sources; ++s) {
            collect(s);
        }
        model.step();
    }
    if (!model.drained() || !expected.empty()) {
        throw std::runtime_error("benchmark 未能排空");
    }
    std::cout << "{\"bytes\":" << endBytes - beginBytes
              << ",\"window_ps\":" << window * period
              << ",\"peak_slots\":" << peak << ",\"max_bank_wait\":" << maxWait
              << ",\"read_commands\":" << model.stats().reads
              << ",\"write_commands\":" << model.stats().writes << "}\n";
}
} // namespace

/** 解析夹具参数及独立参考工具送入的命令。 */
int
main(int argc, char **argv)
{
    try {
        SramModel::Config cfg;
        std::string kind;
        uint64_t period = 1000;
        bool tracing = false;
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--compute") {
                cfg.banks = 16;
                cfg.wordBytes = 16;
                cfg.sources = 5;
                cfg.interleaved = true;
            } else if (arg == "--split") {
                cfg.splitPorts = true;
            } else if (arg == "--trace") {
                tracing = true;
            } else if (arg == "--bench") {
                kind = argv[++i];
            } else if (arg == "--period") {
                period = std::stoull(argv[++i]);
            } else if (arg == "--latency") {
                cfg.readCycles = cfg.writeCycles = std::stoul(argv[++i]);
            } else if (arg == "--interval") {
                cfg.interval = std::stoul(argv[++i]);
            } else if (arg == "--word") {
                cfg.wordBytes = std::stoul(argv[++i]);
            } else if (arg == "--slots") {
                cfg.slots = std::stoul(argv[++i]);
            } else if (arg == "--interleaved") {
                cfg.interleaved = true;
            } else {
                throw std::invalid_argument("未知 SRAM probe 参数");
            }
        }
        SramModel model(cfg);
        ready(model);
        if (!kind.empty()) {
            benchmark(model, kind, period);
            return 0;
        }
        if (tracing) {
            model.trace = [](const auto &event, auto source, auto id,
                             auto address, auto cycle) {
                std::cout << "{\"event\":\"" << event
                          << "\",\"source\":" << source << ",\"id\":" << id
                          << ",\"address\":" << address
                          << ",\"cycle\":" << cycle << "}\n";
            };
        }
        std::string line;
        while (std::getline(std::cin, line)) {
            std::istringstream command(line);
            std::string opcode;
            command >> opcode;
            if (opcode.empty()) {
                continue;
            }
            unsigned source = 0, count = 0;
            uint64_t address = 0;
            if (opcode == "S") {
                command >> count;
                while (count--) {
                    model.step();
                }
            } else if (opcode == "W" || opcode == "R") {
                SramModel::Request request;
                command >> source >> request.address >> request.size;
                request.write = opcode == "W";
                unsigned seed = 0;
                if (request.write) {
                    command >> request.mask >> seed;
                    for (unsigned i = 0; i < 64; ++i) {
                        request.data[i] = (seed + i) & 255;
                    }
                }
                uint64_t id = 0;
                bool accepted = model.submit(source, request, id);
                std::cout << "{\"submit\":" << id
                          << ",\"accepted\":" << (accepted ? "true" : "false")
                          << ",\"cycle\":" << model.now() << "}\n";
            } else if (opcode == "E") {
                uint64_t value;
                command >> value;
                std::cout << "{\"ecc\":" << unsigned(SramModel::encode(value))
                          << "}\n";
            } else if (opcode == "D") {
                uint64_t value;
                unsigned check;
                command >> value >> check;
                auto result = SramModel::decode(value, check);
                std::cout << "{\"data\":" << result.data
                          << ",\"ce\":" << result.corrected
                          << ",\"ue\":" << result.uncorrectable << "}\n";
            } else if (opcode == "T") {
                command >> source;
                completion(model, source);
            } else if (opcode == "I") {
                command >> address >> count;
                model.inject(address, count);
            } else if (opcode == "RESET") {
                command >> count;
                model.reset(count);
            } else if (opcode == "PAUSE") {
                model.pause();
            } else if (opcode == "RESUME") {
                model.resume();
            } else if (opcode == "RETAIN") {
                if (!model.enterRetention()) {
                    throw std::runtime_error("不能进入保持");
                }
            } else if (opcode == "LEAVE") {
                model.leaveRetention();
            } else {
                throw std::invalid_argument("未知 SRAM probe 命令");
            }
        }
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
