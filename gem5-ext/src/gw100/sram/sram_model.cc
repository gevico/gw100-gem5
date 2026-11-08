/**
 * @file  sram_model.cc
 * @brief SRAM 数据、端口流水、仲裁、ECC 与生命周期
 * @see spec-sram
 */
#include "src/gw100/sram/sram_model.hh"

#include <algorithm>
#include <cstring>
#include <istream>
#include <limits>
#include <ostream>
#include <sstream>
#include <stdexcept>

namespace gem5::gw100
{
namespace
{
constexpr uint64_t Never = std::numeric_limits<uint64_t>::max();
/** 检查非零二的幂。 */
bool
powerOfTwo(uint64_t value)
{ return value && !(value & (value - 1)); }
/** 生成 1–64 B 请求的字节使能掩码。 */
uint64_t
maskFor(unsigned size)
{ return size == 64 ? ~uint64_t(0) : (uint64_t(1) << size) - 1; }
/** 按小端序从八个字节读取数据。 */
uint64_t
load64(const uint8_t *data)
{
    uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i) {
        value |= uint64_t(data[i]) << (8 * i);
    }
    return value;
}
/** 按小端序写出八个数据字节。 */
} // namespace

struct SramModel::Operation
{
    /** 宏字偏移、首次队首周期及累计合格仲裁等待。 */
    uint64_t word = 0, headAt = Never, arbitrationWait = 0;
    /** 是否已发出、已结束及需要读改写。 */
    bool issued = false, done = false, rmw = false;
};
struct SramModel::Transaction
{
    /** 原始父请求。 */
    Request request;
    /** 持有至响应接受的完成记录。 */
    Completion result;
    /** 硬件来源和未结束的子操作数量。 */
    unsigned source = 0, remaining = 0;
    /** 该请求最早可进入 bank 仲裁的有效周期。 */
    uint64_t serviceAt = 0;
    /** 最多九个宏字拆分项。 */
    std::vector<Operation> operations;
    /** 是否发生 UE，以及是否已经进入返回队列。 */
    bool failed = false, queued = false;
};
struct SramModel::Active
{
    /** 父槽编号和该父请求内的操作编号。 */
    unsigned slot, op;
    /** 该阶段的到期有效周期。 */
    uint64_t due;
    /** 当前物理流水阶段。 */
    enum Stage
    {
        /** 读取返回流水。 */
        Read,
        /** 等待实际写提交。 */
        Write,
        /** 等待读改写的写端口发出。 */
        Modify
    } stage;
    /** 采样值或待提交的新宏字。 */
    std::array<uint8_t, 64> data{};
};
struct SramModel::Bank
{
    /** 读、写物理端口的最早可发出周期。 */
    uint64_t readNext = 0, writeNext = 0;
    /** 读、写仲裁的下一优先来源。 */
    unsigned readRound = 0, writeRound = 0;
    /** 每来源的父槽编号及子操作编号 FIFO。 */
    std::vector<std::deque<std::pair<unsigned, unsigned>>> queues;
};

/** 拒绝组织、时序或有限资源范围不合法的配置。 */
void
SramModel::Config::validate() const
{
    if (!powerOfTwo(banks) || !powerOfTwo(wordBytes) || wordBytes < 8 ||
        wordBytes > 64 || !powerOfTwo(maskBytes) || maskBytes > wordBytes ||
        !size || size % (uint64_t(banks) * wordBytes) || !sources ||
        sources > 64 || !slots || slots > 256 || !readCycles || !writeCycles ||
        !interval) {
        throw std::invalid_argument("非法 SRAM 组织或时序参数");
    }
}

/** 返回 checkpoint 所需的规范化配置指纹。 */
std::string
SramModel::Config::fingerprint() const
{
    std::ostringstream out;
    out << size << ',' << banks << ',' << wordBytes << ',' << sources << ','
        << slots << ',' << readCycles << ',' << writeCycles << ',' << interval
        << ',' << maskBytes << ',' << interleaved << ',' << splitPorts << ','
        << ecc;
    return out.str();
}

/** 校验配置并分配独立的数据、ECC 和有限事务资源。 */
SramModel::SramModel(const Config &config) : cfg(config)
{
    cfg.validate();
    if (cfg.ecc) {
        throw std::invalid_argument("本阶段仅开放 ECC 关闭的独立配置");
    }
    bytes.resize(cfg.size);
    checks.resize(cfg.size / 8);
    transactions.resize(cfg.sources * cfg.slots);
    banks.resize(cfg.banks);
    for (auto &bank : banks) {
        bank.queues.resize(cfg.sources);
    }
    responses.resize(cfg.sources);
    lastAccept.assign(cfg.sources, Never);
    lastResponse.assign(cfg.sources, Never);
}
/** 释放实例拥有的状态。 */
SramModel::~SramModel() = default;

/** 以固定 Hamming 编号生成八位偶校验。 */
uint8_t
SramModel::encode(uint64_t data)
{
    // Hamming 位置 1..71，2 的幂为校验位，偶校验扩展位保存在 bit 7。
    unsigned index = 0;
    uint8_t parity = 0;
    for (unsigned position = 1; position <= 71; ++position) {
        if (powerOfTwo(position)) {
            continue;
        }
        if ((data >> index++) & 1) {
            parity ^= position;
        }
    }
    unsigned overall =
        (__builtin_popcountll(data) + __builtin_popcount(unsigned(parity))) &
        1;
    return parity | (overall << 7);
}

/** 检查并纠正一个 64+8 SECDED 码字。 */

/** 将合法实例内地址映射至 bank 编号。 */
unsigned
SramModel::bankOf(uint64_t address) const
{
    return cfg.interleaved ? (address / cfg.wordBytes) % cfg.banks
                           : address / (cfg.size / cfg.banks);
}

/** 发出带父事务身份的周期 trace。 */
void
SramModel::emit(const std::string &event, const Transaction &txn,
                uint64_t address)
{
    if (trace) {
        trace(event, txn.source, txn.result.id, address, cycle);
    }
}

/** cancelledEpoch 由端口外壳标记已退役的入口排队请求。 */
bool
SramModel::submit(unsigned source, const Request &request, uint64_t &id,
                  uint64_t cancelledEpoch)
{
    if (source >= cfg.sources) {
        throw std::out_of_range("SRAM 来源越界");
    }
    if (lastAccept[source] == cycle) {
        return false;
    }
    unsigned first = source * cfg.slots, slot = first;
    while (slot < first + cfg.slots && transactions[slot]) {
        ++slot;
    }
    if (slot == first + cfg.slots) {
        return false;
    }
    auto txn = std::make_unique<Transaction>();
    txn->request = request;
    txn->source = source;
    txn->result.id = nextId++;
    txn->result.epoch = generation;
    txn->result.address = request.address;
    txn->result.accepted = cycle;
    txn->serviceAt = cycle + (admitting ? 0 : 1);
    if (cancelledEpoch != UINT64_MAX) {
        txn->result.status = Status::Reset;
        txn->result.epoch = cancelledEpoch;
    } else if (!request.size || request.size > 64 ||
               request.address >= cfg.size ||
               request.size > cfg.size - request.address ||
               (request.write && (request.mask & ~maskFor(request.size)))) {
        txn->result.status = Status::Address;
    } else if (mode != State::Ready) {
        txn->result.status = Status::NotReady;
    } else {
        uint64_t firstWord = request.address / cfg.wordBytes;
        uint64_t lastWord =
            (request.address + request.size - 1) / cfg.wordBytes;
        for (uint64_t word = firstWord; word <= lastWord; ++word) {
            Operation op;
            op.word = word * cfg.wordBytes;
            if (request.write) {
                unsigned granule =
                    cfg.ecc ? std::max(8u, cfg.maskBytes) : cfg.maskBytes;
                for (unsigned off = 0; off < cfg.wordBytes; off += granule) {
                    unsigned enabled = 0;
                    for (unsigned i = 0; i < granule; ++i) {
                        uint64_t address = op.word + off + i;
                        if (address >= request.address &&
                            address - request.address < request.size &&
                            ((request.mask >> (address - request.address)) &
                             1)) {
                            ++enabled;
                        }
                    }
                    op.rmw |= enabled && enabled != granule;
                }
            }
            txn->operations.push_back(op);
            unsigned opIndex = txn->operations.size() - 1;
            banks[bankOf(op.word)].queues[source].emplace_back(slot, opIndex);
        }
        txn->remaining = txn->operations.size();
    }
    lastAccept[source] = cycle;
    id = txn->result.id;
    ++counters.accepted;
    emit("accept", *txn, request.address);
    transactions[slot] = std::move(txn);
    return true;
}

/** 采样所需码字并报告 CE 或 UE。 */
bool
SramModel::readWord(Transaction &, Operation &op,
                    std::array<uint8_t, 64> &data)
{
    std::copy_n(bytes.begin() + op.word, cfg.wordBytes, data.begin());
    return true;
}

/** 提交宏字内已使能的字节并更新 ECC 与提交掩码。 */
void
SramModel::commit(Transaction &txn, const Operation &op,
                  const std::array<uint8_t, 64> &data)
{
    const auto &request = txn.request;
    for (unsigned off = 0; off < cfg.wordBytes; off += 8) {
        bool touched = false;
        for (unsigned i = 0; i < 8; ++i) {
            uint64_t address = op.word + off + i;
            if (address >= request.address &&
                address - request.address < request.size &&
                ((request.mask >> (address - request.address)) & 1)) {
                touched = true;
                bytes[address] = data[off + i];
                txn.result.committed |= uint64_t(1)
                                        << (address - request.address);
                ++counters.writeBytes;
            } else if (op.rmw && cfg.ecc) {
                // 被修改码字的纠正值随 RMW 一起写回。
                bool wordTouched = false;
                for (unsigned j = 0; j < 8; ++j) {
                    uint64_t a = op.word + off + j;
                    wordTouched |=
                        a >= request.address &&
                        a - request.address < request.size &&
                        ((request.mask >> (a - request.address)) & 1);
                }
                if (wordTouched) {
                    bytes[address] = data[off + i];
                }
            }
        }
        if (touched) {
            checks[(op.word + off) / 8] =
                encode(load64(&bytes[op.word + off]));
        }
    }
    emit("commit", txn, op.word);
}

/** 全部子操作结束时把父槽转入返回队列。 */
void
SramModel::finish(unsigned slot)
{
    auto &txn = *transactions[slot];
    if (txn.queued || txn.remaining) {
        return;
    }
    if (txn.failed) {
        txn.result.status =
            txn.result.committed ? Status::Partial : Status::Ecc;
    }
    txn.result.ready = cycle;
    txn.queued = true;
    responses[txn.source].push_back(slot);
    ++counters.completed;
    counters.rejected += txn.result.status != Status::Ok;
    emit("ready", txn, txn.request.address);
}

/** 提交到期写入并推进读或 RMW 流水。 */
void
SramModel::completeOperations()
{
    for (auto it = active.begin(); it != active.end();) {
        if (it->due > cycle) {
            ++it;
            continue;
        }
        auto &txn = *transactions[it->slot];
        auto &op = txn.operations[it->op];
        if (it->stage == Active::Modify && !txn.failed) {
            auto &bank = banks[bankOf(op.word)];
            if (bank.writeNext > cycle) {
                ++it;
                continue;
            }
            bank.writeNext = cycle + cfg.interval;
            if (!cfg.splitPorts) {
                bank.readNext = bank.writeNext;
            }
            ++counters.writes;
            emit("issue_write", txn, op.word);
            it->stage = Active::Write;
            it->due = cycle + cfg.writeCycles;
            ++it;
            continue;
        }
        if (it->stage == Active::Write) {
            commit(txn, op, it->data);
        }
        op.done = true;
        --txn.remaining;
        unsigned slot = it->slot;
        it = active.erase(it);
        finish(slot);
    }
}

/** 检查宏字依赖并累计相应等待。 */
bool
SramModel::eligible(unsigned slot, unsigned index)
{
    auto &txn = *transactions[slot];
    auto &op = txn.operations[index];
    if (op.headAt == Never) {
        op.headAt = cycle;
        counters.queueWait += cycle - txn.serviceAt;
    }
    // 所有在途写和 RMW 都持有宏字锁，读采样完成后可释放读依赖。
    for (const auto &pending : active) {
        const auto &other = *transactions[pending.slot];
        if (pending.stage != Active::Read &&
            other.operations[pending.op].word == op.word) {
            if (other.source == txn.source) {
                ++counters.dependencyWait;
            } else {
                ++op.arbitrationWait;
                ++counters.bankWait;
                counters.maxBankWait =
                    std::max(counters.maxBankWait, op.arbitrationWait);
            }
            return false;
        }
    }
    return true;
}

/** 发出宏操作，采样读数据或建立延后提交的写流水。 */
void
SramModel::issue(unsigned slot, unsigned index, unsigned bankIndex)
{
    auto &txn = *transactions[slot];
    auto &op = txn.operations[index];
    op.issued = true;
    Active pending{slot, index, cycle, Active::Read, {}};
    bool reading = !txn.request.write || op.rmw;
    if (reading) {
        ++counters.reads;
        counters.rmw += op.rmw;
        emit("issue_read", txn, op.word);
        if (op.rmw) {
            emit("rmw", txn, op.word);
        } else if (txn.request.instruction) {
            emit("fetch", txn, op.word);
        }
        if (!readWord(txn, op, pending.data)) {
            txn.failed = true;
        }
        emit("sample", txn, op.word);
        if (!txn.request.write && !txn.failed) {
            for (unsigned i = 0; i < cfg.wordBytes; ++i) {
                uint64_t address = op.word + i;
                if (address >= txn.request.address &&
                    address - txn.request.address < txn.request.size) {
                    txn.result.data[address - txn.request.address] =
                        pending.data[i];
                    ++counters.readBytes;
                }
            }
        }
        pending.stage = op.rmw ? Active::Modify : Active::Read;
        pending.due = cycle + cfg.readCycles;
    } else {
        ++counters.writes;
        emit("issue_write", txn, op.word);
        pending.stage = Active::Write;
        pending.due = cycle + cfg.writeCycles;
    }
    if (txn.request.write) {
        for (unsigned i = 0; i < cfg.wordBytes; ++i) {
            uint64_t address = op.word + i;
            if (address >= txn.request.address &&
                address - txn.request.address < txn.request.size &&
                ((txn.request.mask >> (address - txn.request.address)) & 1)) {
                pending.data[i] =
                    txn.request.data[address - txn.request.address];
            }
        }
    }
    auto &bank = banks[bankIndex];
    if (!cfg.splitPorts) {
        bank.readNext = bank.writeNext = cycle + cfg.interval;
    } else if (reading) {
        bank.readNext = cycle + cfg.interval;
    } else {
        bank.writeNext = cycle + cfg.interval;
    }
    active.push_back(pending);
}

/** 按轮询为一个物理端口选择合格的来源队首。 */
bool
SramModel::grant(unsigned bankIndex, bool writePort,
                 std::vector<uint64_t> &issued)
{
    auto &bank = banks[bankIndex];
    uint64_t available = writePort ? bank.writeNext : bank.readNext;
    unsigned &round = writePort ? bank.writeRound : bank.readRound;
    // 即使端口忙也记录队首首次具备条件的时间。
    std::vector<bool> ready(cfg.sources);
    for (unsigned source = 0; source < cfg.sources; ++source) {
        auto &queue = bank.queues[source];
        while (!queue.empty()) {
            auto [slot, index] = queue.front();
            auto &txn = *transactions[slot];
            if (!txn.failed) {
                break;
            }
            txn.operations[index].done = true;
            --txn.remaining;
            queue.pop_front();
            finish(slot);
        }
        if (queue.empty()) {
            continue;
        }
        auto [slot, index] = queue.front();
        auto &txn = *transactions[slot];
        auto &op = txn.operations[index];
        bool isWrite = txn.request.write && !op.rmw;
        if (cfg.splitPorts && isWrite != writePort) {
            continue;
        }
        ready[source] = eligible(slot, index);
    }
    unsigned selected = cfg.sources;
    for (unsigned offset = 0; offset < cfg.sources; ++offset) {
        unsigned source = (round + offset) % cfg.sources;
        auto &queue = bank.queues[source];
        if (!ready[source] || queue.empty()) {
            continue;
        }
        auto [slot, index] = queue.front();
        auto &op = transactions[slot]->operations[index];
        if (available > cycle ||
            std::find(issued.begin(), issued.end(), op.word) != issued.end()) {
            continue;
        }
        issued.push_back(op.word);
        queue.pop_front();
        round = (source + 1) % cfg.sources;
        issue(slot, index, bankIndex);
        selected = source;
        break;
    }
    for (unsigned source = 0; source < cfg.sources; ++source) {
        if (!ready[source] || source == selected) {
            continue;
        }
        auto [slot, index] = bank.queues[source].front();
        auto &op = transactions[slot]->operations[index];
        ++op.arbitrationWait;
        ++counters.bankWait;
        counters.maxBankWait =
            std::max(counters.maxBankWait, op.arbitrationWait);
    }
    return selected != cfg.sources;
}

/** 先完成到期操作，再调用准入阶段并仲裁，暂停时保持状态。 */
void
SramModel::step(const std::function<void()> &admit)
{
    if (paused) {
        return;
    }
    ++cycle;
    for (unsigned slot = 0; slot < transactions.size(); ++slot) {
        if (transactions[slot] && !transactions[slot]->remaining) {
            finish(slot);
        }
    }
    if (mode == State::Initializing) {
        uint64_t words = cfg.size / cfg.banks / cfg.wordBytes;
        while (!initPending.empty() && initPending.front().first <= cycle) {
            uint64_t word = initPending.front().second;
            for (unsigned b = 0; b < cfg.banks; ++b) {
                uint64_t address =
                    cfg.interleaved
                        ? (word * cfg.banks + b) * cfg.wordBytes
                        : b * (cfg.size / cfg.banks) + word * cfg.wordBytes;
                std::fill_n(bytes.begin() + address, cfg.wordBytes, 0);
                std::fill_n(checks.begin() + address / 8, cfg.wordBytes / 8,
                            encode(0));
                ++counters.initWrites;
            }
            initPending.pop_front();
        }
        if (initWord < words && cycle >= initRemaining) {
            initPending.emplace_back(cycle + cfg.writeCycles, initWord);
            initRemaining = cycle + cfg.interval;
            for (auto &bank : banks) {
                bank.writeNext = initRemaining;
                if (!cfg.splitPorts) {
                    bank.readNext = initRemaining;
                }
            }
            ++initWord;
        }
        if (initWord == words && initPending.empty()) {
            mode = State::Ready;
        }
    }
    completeOperations();
    if (admit) {
        admitting = true;
        admit();
        admitting = false;
    }
    for (unsigned slot = 0; slot < transactions.size(); ++slot) {
        if (transactions[slot] && !transactions[slot]->remaining) {
            finish(slot);
        }
    }
    if (mode != State::Ready) {
        return;
    }
    for (unsigned b = 0; b < cfg.banks; ++b) {
        std::vector<uint64_t> issued;
        grant(b, false, issued);
        if (cfg.splitPorts) {
            grant(b, true, issued);
        }
    }
}

/** 返回来源的首个就绪响应，每周期最多一笔。 */
const SramModel::Completion *
SramModel::response(unsigned source) const
{
    if (responses.at(source).empty() || lastResponse[source] == cycle) {
        return nullptr;
    }
    return &transactions[responses[source].front()]->result;
}

/** 接受来源响应并释放其完整父事务槽。 */
bool
SramModel::acceptResponse(unsigned source)
{
    auto result = response(source);
    if (!result) {
        return false;
    }
    unsigned slot = responses[source].front();
    auto &txn = *transactions[slot];
    counters.responseWait += cycle - txn.result.ready;
    emit("response", txn, txn.request.address);
    responses[source].pop_front();
    transactions[slot].reset();
    lastResponse[source] = cycle;
    return true;
}

/** 检查该来源的请求、活动操作与返回是否均已收尾。 */
bool
SramModel::sourceDrained(unsigned source) const
{
    for (unsigned slot = source * cfg.slots; slot < (source + 1) * cfg.slots;
         ++slot) {
        if (transactions.at(slot)) {
            return false;
        }
    }
    return true;
}
/** 检查全部事务、初始化与端口 II 是否均已完成。 */
bool
SramModel::drained() const
{
    if (mode == State::Initializing || !active.empty()) {
        return false;
    }
    for (unsigned s = 0; s < cfg.sources; ++s) {
        if (!sourceDrained(s)) {
            return false;
        }
    }
    for (const auto &bank : banks) {
        if (bank.readNext > cycle || bank.writeNext > cycle) {
            return false;
        }
    }
    return true;
}
/** 检查是否还需要有效时钟边沿。 */
bool
SramModel::busy() const
{ return !paused && !drained(); }

/** 优先取消未提交操作，按 preserve 决定是否保持数据状态。 */
void
SramModel::reset(bool preserve)
{
    State previous = mode;
    ++generation;
    active.clear();
    initPending.clear();
    for (auto &bank : banks) {
        for (auto &queue : bank.queues) {
            queue.clear();
        }
        bank.readNext = bank.writeNext = cycle;
        bank.readRound = bank.writeRound = 0;
    }
    for (unsigned slot = 0; slot < transactions.size(); ++slot) {
        if (!transactions[slot]) {
            continue;
        }
        auto &txn = *transactions[slot];
        txn.remaining = 0;
        txn.failed = false;
        txn.result.status = Status::Reset;
        finish(slot);
    }
    mode = preserve ? previous : State::Reset;
    if (mode == State::Initializing) {
        initWord = initRemaining = 0;
    }
    paused = false;
}
/** 从排空状态开始逐 bank 清零。 */
void
SramModel::initialize()
{
    if (!drained()) {
        throw std::logic_error("SRAM 清零要求旧代次响应已收尾");
    }
    mode = State::Initializing;
    initWord = initRemaining = 0;
}
/** 排空且就绪时进入保持，返回是否成功。 */
/** 从保持恢复到就绪。 */
/** 验证实例内非空字节范围且避免地址溢出。 */
void
SramModel::checkRange(uint64_t address, unsigned size) const
{
    if (!size || address >= cfg.size || size > cfg.size - address) {
        throw std::out_of_range("SRAM 地址越界");
    }
}
/** 在对齐码字中翻转编号 0–71 的一个物理位。 */
/** 排空后读取原始数据，供调试和兼容路径使用。 */
void
SramModel::functionalRead(uint64_t address, uint8_t *data, unsigned size) const
{
    checkRange(address, size);
    if (!drained()) {
        throw std::logic_error("功能访问要求排空");
    }
    std::copy_n(bytes.begin() + address, size, data);
}
/** 排空后写入原始数据并重建 ECC，供加载和兼容路径使用。 */
void
SramModel::functionalWrite(uint64_t address, const uint8_t *data,
                           unsigned size)
{
    checkRange(address, size);
    if (!drained()) {
        throw std::logic_error("功能访问要求排空");
    }
    std::copy_n(data, size, bytes.begin() + address);
    for (uint64_t word = address / 8; word <= (address + size - 1) / 8;
         ++word) {
        checks[word] = encode(load64(&bytes[word * 8]));
    }
}
/** 保存已排空的 Ready 或 Retention 状态。 */
/** 校验完整快照后原子恢复，失败时保留原实例。 */
} // namespace gem5::gw100
