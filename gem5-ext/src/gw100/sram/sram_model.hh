/**
 * @file  sram_model.hh
 * @brief 有限资源的 SRAM 周期状态机与 SECDED
 * @see spec-sram
 */
#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <iosfwd>
#include <memory>
#include <string>
#include <vector>

#include "src/gw100/sram/generated/sram_defs.hh"

namespace gem5::gw100
{

/** 共享 SRAM 的纯数字状态机，cycle 表示最近处理的边沿。 */
class SramModel
{
  public:
    /** 独立实例的物理组织与有效时钟周期数。 */
    struct Config
    {
        /** 数据容量，单位为字节。 */
        uint64_t size = sramdefs::MANAGEMENT_SIZE;
        /** 并行 bank 数，为二的幂。 */
        unsigned banks = sramdefs::MANAGEMENT_BANKS;
        /** 宏数据字宽，支持 8、16、32、64 B。 */
        unsigned wordBytes = sramdefs::MANAGEMENT_WORD_BYTES;
        /** 绑定硬件入口的来源数，范围为 1–64。 */
        unsigned sources = sramdefs::MANAGEMENT_SOURCES;
        /** 每来源父事务槽，范围为 1–256。 */
        unsigned slots = sramdefs::MANAGEMENT_SLOTS;
        /** 采样至读结果就绪的有效周期数。 */
        unsigned readCycles = sramdefs::MANAGEMENT_READ_CYCLES;
        /** 发出至写提交的有效周期数。 */
        unsigned writeCycles = sramdefs::MANAGEMENT_WRITE_CYCLES;
        /** 同物理端口相邻发出的最小周期数。 */
        unsigned interval = sramdefs::MANAGEMENT_INTERVAL_CYCLES;
        /** 宏写掩码粒度，单位为字节。 */
        unsigned maskBytes = sramdefs::MANAGEMENT_MASK_BYTES;
        /** true 采用宏字交织，false 采用连续 bank 分区。 */
        bool interleaved = sramdefs::MANAGEMENT_INTERLEAVED;
        /** true 为 1R1W，false 为 1RW。 */
        bool splitPorts = sramdefs::MANAGEMENT_SPLIT_PORTS;
        /** 每 8 B 数据启用 8 位 SECDED。 */
        bool ecc = sramdefs::MANAGEMENT_ECC;
        /** 拒绝组织、时序或有限资源范围不合法的配置。 */
        void validate() const;
        /** 返回 checkpoint 所需的规范化配置指纹。 */
        std::string fingerprint() const;
    };

    /** 事务完成状态，部分效果由 committed 掩码进一步描述。 */
    enum class Status
    {
        /** 全部操作成功。 */
        Ok,
        /** 请求地址、长度或掩码不合法。 */
        Address,
        /** 实例当前未开放普通访问。 */
        NotReady,
        /** UE 导致失败，尚无写入效果。 */
        Ecc,
        /** 请求所属代次已被复位取消。 */
        Reset,
        /** 多宏字事务失败且已有部分字节提交。 */
        Partial
    };
    /** 初始化与数据保持状态。 */
    enum class State
    {
        /** 复位后等待显式初始化。 */
        Reset,
        /** 正通过宏端口清零。 */
        Initializing,
        /** 开放普通数据访问。 */
        Ready,
        /** 数据保持，普通请求返回未就绪。 */
        Retention
    };
    /** 单个 1–64 B 逻辑请求，来源由 submit 的入口绑定。 */
    struct Request
    {
        /** 实例内的字节偏移。 */
        uint64_t address = 0;
        /** 请求长度，单位为字节。 */
        unsigned size = 8;
        /** true 为写入，false 为读取。 */
        bool write = false;
        /** 读取是否来自取指端，用于可控故障夹具。 */
        bool instruction = false;
        /** bit i 使能请求内的字节 i。 */
        uint64_t mask = 0xff;
        /** 写入数据，读取时忽略。 */
        std::array<uint8_t, 64> data{};
    };
    /** 保留至来源接受的有界完成记录。 */
    struct Completion
    {
        /** 实例内唯一事务号。 */
        uint64_t id = 0;
        /** 请求接受时所属的复位代次。 */
        uint64_t epoch = 0;
        /** 原始实例内偏移。 */
        uint64_t address = 0;
        /** bit i 表示请求内字节 i 已提交。 */
        uint64_t committed = 0;
        /** 阵列接受请求的有效周期编号。 */
        uint64_t accepted = 0;
        /** 响应就绪的有效周期编号。 */
        uint64_t ready = 0;
        /** 完成状态。 */
        Status status = Status::Ok;
        /** 成功读取的数据，按请求内偏移排列。 */
        std::array<uint8_t, 64> data{};
    };
    /** 单个 72 位码字的解码结果。 */
    struct EccResult
    {
        /** 纠正后的 64 位数据。 */
        uint64_t data;
        /** 检测并纠正单比特错误。 */
        bool corrected;
        /** 检测到不可纠正错误。 */
        bool uncorrectable;
    };
    /** 累计计数，等待量的单位为有效 SRAM 周期。 */
    struct Stats
    {
        /** 准入、响应就绪、错误完成的父事务数量。 */
        uint64_t accepted = 0, completed = 0, rejected = 0;
        /** 宏读、宏写、RMW、纠正码字、UE 码字数量。 */
        uint64_t reads = 0, writes = 0, rmw = 0, ce = 0, ue = 0;
        /** 有效读取和写入字节数，以及初始化提交的宏字数。 */
        uint64_t readBytes = 0, writeBytes = 0, initWrites = 0;
        /** 最长 bank 等待、累计 bank 等待、累计返回等待。 */
        uint64_t maxBankWait = 0, bankWait = 0, responseWait = 0;
        /** 来源内部队列等待和自身宏字依赖等待。 */
        uint64_t queueWait = 0, dependencyWait = 0;
    };
    /** trace 与 ECC 事件的时间单位为 SRAM 周期。 */
    std::function<void(const std::string &, unsigned, uint64_t, uint64_t,
                       uint64_t)>
        trace;
    /** ECC 事件，参数为实例内码字地址与 UE 标志。 */
    std::function<void(uint64_t, bool)> eccEvent;

    /** 校验配置并分配独立的数据、ECC 和有限事务资源。 */
    explicit SramModel(const Config &config);
    /** 释放实例拥有的状态。 */
    ~SramModel();
    /** 返回实际组织与周期参数。 */
    const Config &
    config() const
    { return cfg; }
    /** 返回初始化或保持状态。 */
    State
    state() const
    { return mode; }
    /** 返回最近处理的有效周期编号。 */
    uint64_t
    now() const
    { return cycle; }
    /** 返回当前复位代次。 */
    uint64_t
    epoch() const
    { return generation; }
    /** 返回累计统计。 */
    const Stats &
    stats() const
    { return counters; }
    /** cancelledEpoch 由端口外壳标记已退役的入口排队请求。 */
    bool submit(unsigned source, const Request &request, uint64_t &id,
                uint64_t cancelledEpoch = UINT64_MAX);
    /** 先完成到期操作，再调用准入阶段并仲裁，暂停时保持状态。 */
    void step(const std::function<void()> &admit = {});
    /** 返回来源的首个就绪响应，每周期最多一笔。 */
    const Completion *response(unsigned source) const;
    /** 接受来源响应并释放其完整父事务槽。 */
    bool acceptResponse(unsigned source);
    /** 检查该来源的请求、活动操作与返回是否均已收尾。 */
    bool sourceDrained(unsigned source) const;
    /** 检查全部事务、初始化与端口 II 是否均已完成。 */
    bool drained() const;
    /** 检查是否还需要有效时钟边沿。 */
    bool busy() const;
    /** 从排空状态开始逐 bank 清零。 */
    void initialize();
    /** 优先取消未提交操作，按 preserve 决定是否保持数据状态。 */
    void reset(bool preserve);
    /** 排空且就绪时进入保持，返回是否成功。 */
    bool enterRetention();
    /** 从保持恢复到就绪。 */
    void leaveRetention();
    /** 暂停有效周期，保留全部剩余流水周期。 */
    void
    pause()
    { paused = true; }
    /** 恢复有效周期推进。 */
    void
    resume()
    { paused = false; }
    /** 返回有效周期是否暂停。 */
    bool
    isPaused() const
    { return paused; }
    /** 将合法实例内地址映射至 bank 编号。 */
    unsigned bankOf(uint64_t address) const;
    /** 在对齐码字中翻转编号 0–71 的一个物理位。 */
    void inject(uint64_t codeword, unsigned bit);
    /** 排空后读取原始数据，供调试和兼容路径使用。 */
    void functionalRead(uint64_t address, uint8_t *data, unsigned size) const;
    /** 排空后写入原始数据并重建 ECC，供加载和兼容路径使用。 */
    void functionalWrite(uint64_t address, const uint8_t *data, unsigned size);
    /** 保存已排空的 Ready 或 Retention 状态。 */
    void save(std::ostream &out) const;
    /** 校验完整快照后原子恢复，失败时保留原实例。 */
    void restore(std::istream &in);
    /** 以固定 Hamming 编号生成八位偶校验。 */
    static uint8_t encode(uint64_t data);
    /** 检查并纠正一个 64+8 SECDED 码字。 */
    static EccResult decode(uint64_t data, uint8_t check);

  private:
    /** 单个宏字子操作。 */
    struct Operation;
    /** 父请求、拆分项与完成槽。 */
    struct Transaction;
    /** 已发出且尚未到期的流水操作。 */
    struct Active;
    /** 单 bank 的端口时间与来源队列。 */
    struct Bank;
    /** 本实例的冻结组织。 */
    Config cfg;
    /** 当前初始化或保持状态。 */
    State mode = State::Reset;
    /** 有效周期、复位代次和下一个事务号。 */
    uint64_t cycle = 0, generation = 0, nextId = 1;
    /** 下一个清零宏字索引和最早发出周期。 */
    uint64_t initWord = 0, initRemaining = 0;
    /** 有效时钟是否暂停。 */
    bool paused = false;
    /** 当前是否位于完成处理之后、仲裁之前的边沿准入阶段。 */
    bool admitting = false;
    /** 累计统计。 */
    Stats counters;
    /** 数据字节和每 8 B 数据的 ECC 字节。 */
    std::vector<uint8_t> bytes, checks;
    /** 固定来源数乘以槽数的父事务池。 */
    std::vector<std::unique_ptr<Transaction>> transactions;
    /** 独立 bank 状态。 */
    std::vector<Bank> banks;
    /** 由事务和拆分上限约束的活动流水。 */
    std::vector<Active> active;
    /** 每来源的就绪父槽编号队列。 */
    std::vector<std::deque<unsigned>> responses;
    /** 每来源最近准入和响应接受的有效周期。 */
    std::vector<uint64_t> lastAccept, lastResponse;
    /** 清零流水中的到期周期及宏字索引。 */
    std::deque<std::pair<uint64_t, uint64_t>> initPending;
    /** 发出带父事务身份的周期 trace。 */
    void emit(const std::string &event, const Transaction &txn,
              uint64_t address);
    /** 全部子操作结束时把父槽转入返回队列。 */
    void finish(unsigned slot);
    /** 提交到期写入并推进读或 RMW 流水。 */
    void completeOperations();
    /** 按轮询为一个物理端口选择合格的来源队首。 */
    bool grant(unsigned bank, bool writePort, std::vector<uint64_t> &issued);
    /** 检查宏字依赖并累计相应等待。 */
    bool eligible(unsigned slot, unsigned op);
    /** 发出宏操作，采样读数据或建立延后提交的写流水。 */
    void issue(unsigned slot, unsigned op, unsigned bank);
    /** 采样所需码字并报告 CE 或 UE。 */
    bool readWord(Transaction &txn, Operation &op,
                  std::array<uint8_t, 64> &data);
    /** 提交宏字内已使能的字节并更新 ECC 与提交掩码。 */
    void commit(Transaction &txn, const Operation &op,
                const std::array<uint8_t, 64> &data);
    /** 验证实例内非空字节范围且避免地址溢出。 */
    void checkRange(uint64_t address, unsigned size) const;
};

} // namespace gem5::gw100
