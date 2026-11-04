/**
 * @file  hdma_engine.hh
 * @brief Host DMA 引擎：通道、描述符环、SGL、仲裁、缓冲与错误处理
 *
 * 按 spec-host-dma 实现 HDMA 的行为，经 hdma_if.hh 中的接口访问时钟、
 * host 内存、设备内存和 ML，不接触 gem5 的 Packet。
 * - 每个通道预取最多 2 个描述符；不带 FENCE 的描述符在前一个发完全部
 *   请求后即可开始（§5.2）；完成状态按环的顺序回写，回写收到 OK 后推进
 *   HEAD、发出 HDMA_NOTIFY（§5.3）。
 * - 读方向（H2D 数据与 SGL 段）和写方向（D2H 数据）各自仲裁：通道 0
 *   严格优先，通道 1 – 7 加权轮询（§2.3）。
 * - 有限的资源：未完成读请求数受 tag 限制（LRU 分配、超时与复位时隔离，
 *   spec-pcie §10.3），完成缓冲按读请求长度预留，D2H 暂存缓冲按设备
 *   内存读预留（§6）。
 * - 数据通路 256 位：每个周期发一个 MRd；一个 MWr 占 1 + ⌈len/32⌉ 个
 *   周期；写设备内存每 32 B 占一个周期（推算）。
 * - 错误停止、RESUME、ABORT、回写失败、BME 清零、FLR 期间保持复位
 *   （§8）。
 *
 * @see spec-host-dma, spec-pcie
 */

#ifndef __GW100_PCIE_HDMA_HDMA_ENGINE_HH__
#define __GW100_PCIE_HDMA_HDMA_ENGINE_HH__

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <vector>

#include "src/gw100/pcie/hdma/hdma_if.hh"

namespace gem5
{
namespace gw100
{

/** HDMA 的错误码（spec-host-dma §8.1） */
enum class HdmaErr : uint8_t
{
    /** 成功 */
    Ok = 0,
    /** host 读请求收到 UR */
    HostUr = 1,
    /** host 读请求收到 CA */
    HostCa = 2,
    /** host 读请求完成超时 */
    HostTimeout = 3,
    /** poisoned 完成 */
    HostPoisoned = 4,
    /** DPA 无效，或描述符的读取、回写失败 */
    DevAddr = 5,
    /** 读设备内存时不可纠正的 ECC 错误 */
    DevEcc = 6,
    /** 描述符字段非法 */
    DescInvalid = 7,
    /** SGL 违反 §4.2 */
    SglInvalid = 8,
    /** 被中止 */
    Aborted = 9,
    /** host 清除了 Bus Master Enable */
    BusMasterOff = 10,
};

/** Host DMA 引擎 */
class HdmaEngine
{
  public:
    /** 架构参数与实现参数 */
    struct Config
    {
        /** hdma.channels */
        unsigned channels = 8;
        /** log2(hdma.max_ring_entries) */
        unsigned maxRingLog2 = 8;
        /** hdma.max_desc_bytes */
        uint64_t maxDescBytes = 256ULL << 20;
        /** hdma.sgl_segment_max_bytes */
        unsigned sglSegMax = 4096;
        /** hdma.sgl_max_segments */
        unsigned sglMaxSegments = 512;
        /** hdma.max_read_request */
        unsigned maxReadReq = 512;
        /** hdma.max_outstanding_reads */
        unsigned maxOutstandingReads = 64;
        /** hdma.completion_buffer，字节 */
        unsigned cplBuffer = 32 << 10;
        /** hdma.d2h_buffer，字节 */
        unsigned d2hBuffer = 16 << 10;
        /** 设备内存在 DPA 空间中的起始 */
        Addr devBase = 0;
        /** 设备内存的大小 */
        uint64_t devSize = 0;
        /** addr.device_pa_bits */
        unsigned devicePaBits = 40;
        /** host 读请求的完成超时 */
        Tick cplTimeout = 0;
        /** pcie.stale_tag_quarantine */
        Tick quarantine = 0;
        /** 数据通路每周期的字节数 */
        unsigned datapathBytes = 32;
        /** 一次设备内存访问的最大字节数（不跨这个大小的对齐边界） */
        unsigned devChunk = 64;
        /** HDMA_VERSION */
        uint32_t version = 0x0100;
    };

    /** 由配置空间决定的事务参数（spec-pcie §10.3） */
    struct PcieParams
    {
        /** 写请求的最大载荷：min(MPS, 256) */
        unsigned mps = 128;
        /** 读请求的最大长度：min(MRRS, pcie.dma_max_read_request) */
        unsigned mrrs = 512;
        /** Extended Tag Field Enable */
        bool extTag = true;
    };

    /** 按参数构造；各接口由所属设备提供 */
    HdmaEngine(const Config &cfg, HdmaClockIf &clk, HdmaHostIf &host,
               HdmaDevIf &dev, HdmaMlIf &ml);

    /**
     * 读偏移 off 的 32 位寄存器；mirror 为 true 表示经 BAR0 的只读
     * 镜像读取（§7.4），读低位锁存高位的寄存器与固件的锁存互相独立。
     * 未实现的偏移返回 false
     */
    bool read(Addr off, uint32_t &v, bool mirror);

    /** 固件写偏移 off 的 32 位寄存器；未实现的偏移返回 false */
    bool write(Addr off, uint32_t v);

    /** 时钟沿：推进全部通道（由所属设备在 wakeAt 请求的时刻调用） */
    void tick();

    /** 有新的进展可能（回调、端口空闲）时调用：在下一个时钟沿 tick */
    void wake();

    /** 配置空间中的事务参数变化 */
    void setPcie(const PcieParams &p);

    /** Bus Master Enable 的当前值；由 1 变 0 时按 §8.4 停止 */
    void setBusMaster(bool en);

    /**
     * 复位（FLR 开始、设备完全复位）：停止发出 host 请求，在途读请求的
     * tag 进入隔离，撤销 ML 上的请求，寄存器恢复复位值（§8.4）
     */
    void reset();

    /** FLR 进行中保持复位：寄存器读返回复位值，写被忽略，不做任何访问 */
    void setHold(bool h);

    /** host 读请求出错时调用：置 ERR_STATUS.DMA_HOST_ERR（spec-pcie §12.2） */
    std::function<void()> onHostError;

  private:
    /** 通道状态（CH_STATUS.STATE） */
    enum State : uint8_t
    {
        /** 空闲 */
        Idle = 0,
        /** 运行（含中止进行中） */
        Running = 1,
        /** 暂停 */
        Paused = 2,
        /** 错误停止 */
        ErrStop = 3,
    };

    /** 出错的阶段（CH_ERR.PHASE） */
    enum Phase : uint8_t
    {
        /** 数据传输 */
        PhaseData = 0,
        /** 描述符读取 */
        PhaseDescRead = 1,
        /** 描述符回写 */
        PhaseDescWrite = 2,
    };

    /** D2H 暂存缓冲中的一块：一次设备内存读的数据 */
    struct Chunk
    {
        /** 在描述符中的偏移 */
        uint64_t off = 0;
        /** 字节数 */
        unsigned len = 0;
        /** 数据是否已到达 */
        bool arrived = false;
        /** 数据 */
        std::vector<uint8_t> data;
    };

    /** 一个描述符在 HDMA 中的状态 */
    struct Desc
    {
        /** 在环中的下标 */
        unsigned idx = 0;
        /** 唯一编号：回调据此找到描述符 */
        uint64_t serial = 0;
        /** 描述符的读取是否已完成 */
        bool fetched = false;
        /** 描述符的读取失败 */
        bool fetchFailed = false;
        /** 是否已开始 */
        bool started = false;
        /** 全部请求是否已发出（出错或中止后也置位） */
        bool issuedAll = false;
        /** 回写进行中 */
        bool wbPending = false;
        /** TYPE：1 为 H2D，2 为 D2H */
        uint8_t type = 0;
        /** HOST_SGL */
        bool sgl = false;
        /** NOTIFY */
        bool notify = false;
        /** FENCE */
        bool fence = false;
        /** LENGTH */
        uint64_t len = 0;
        /** HOST_ADDR */
        Addr host = 0;
        /** DEV_ADDR */
        Addr dev = 0;
        /** SGL_SEG_BYTES */
        unsigned segBytes = 0;
        /** H2D：已发出读请求的字节数；D2H：已交给发送通路的字节数 */
        uint64_t issued = 0;
        /** D2H：已发出设备内存读的字节数 */
        uint64_t devIssued = 0;
        /** 在途的 host 读和设备内存访问 */
        unsigned outstanding = 0;
        /** 错误码 */
        HdmaErr err = HdmaErr::Ok;
        /** 出错的地址 */
        Addr errAddr = 0;
        /** D2H 暂存缓冲中按顺序排列的块 */
        std::deque<Chunk> staging;
        /** staging 第一块中已经写出的字节数 */
        unsigned frontUsed = 0;
    };

    /** 通道当前描述符的 SGL 遍历状态（每通道最多预取一段，§4.4） */
    struct Sgl
    {
        /** 所属描述符的编号；0 表示未使用 */
        uint64_t owner = 0;
        /** 当前段的地址 */
        Addr segAddr = 0;
        /** 当前段的字节数 */
        unsigned segBytes = 0;
        /** 当前段的内容 */
        std::vector<uint8_t> seg;
        /** 当前段已发出读请求的字节数 */
        unsigned requested = 0;
        /** 当前段已到达的字节数 */
        unsigned arrived = 0;
        /** 下一个要解析的条目 */
        unsigned entry = 0;
        /** 已遍历的段数 */
        unsigned segCount = 0;
        /** 当前段中是否已有 DATA 条目（S8） */
        bool dataInSeg = false;
        /** 当前 DATA 条目剩余的地址 */
        Addr dataAddr = 0;
        /** 当前 DATA 条目剩余的字节数 */
        uint64_t dataLeft = 0;
    };

    /** 通道发现的、尚未写入 CH_ERR 的错误 */
    struct PendingErr
    {
        /** 是否有 */
        bool valid = false;
        /** 错误码 */
        HdmaErr code = HdmaErr::Ok;
        /** 阶段 */
        Phase phase = PhaseData;
        /** 出错描述符的下标 */
        unsigned idx = 0;
        /** 出错的地址 */
        Addr addr = 0;
    };

    /** 一个通道 */
    struct Channel
    {
        /** CH_CTRL 的 RW 位：[0] EN，[1] PAUSE，[11:8] WEIGHT */
        uint32_t ctrl = 0;
        /** CH_RING_BASE */
        Addr ringBase = 0;
        /** CH_RING_SIZE.LOG2_ENTRIES */
        unsigned log2 = 0;
        /** CH_TAIL */
        unsigned tail = 0;
        /** CH_HEAD */
        unsigned head = 0;
        /** 下一个要读取的描述符的下标 */
        unsigned fetchIdx = 0;
        /** 状态 */
        State state = Idle;
        /** CH_ERR */
        uint32_t errReg = 0;
        /** CH_ERR_ADDR */
        uint64_t errAddr = 0;
        /** CH_DESC_DONE */
        uint32_t descDone = 0;
        /** CH_BYTES */
        uint64_t bytes = 0;
        /** 固件读 CH_BYTES_LO 时锁存的高位 */
        uint32_t bytesHiFw = 0;
        /** 镜像读 CH_BYTES_LO 时锁存的高位 */
        uint32_t bytesHiMirror = 0;
        /** 已读取或正在读取的描述符，按环的顺序，最多 2 个 */
        std::deque<Desc> descs;
        /** SGL 遍历状态 */
        Sgl sgl;
        /** 发现了错误，不再开始新的描述符，等已开始的结束后进入错误停止 */
        bool stopping = false;
        /** 待写入 CH_ERR 的错误 */
        PendingErr pending;
        /** 回写失败的错误停止：RESUME 无效，ABORT 不回写 */
        bool wbFailed = false;
        /** 中止进行中 */
        bool aborting = false;
        /** 本通道在途的 host 读和设备内存访问 */
        unsigned inflight = 0;
        /** 中止的回写进行中 */
        bool abortWbPending = false;
        /** 加权轮询中本轮剩余的请求数：读、写、设备内存读三个方向 */
        std::array<unsigned, 3> credit{};
    };

    /** 读请求的用途 */
    enum class ReadKind : uint8_t
    {
        /** H2D 数据 */
        Data,
        /** SGL 段 */
        Sgl,
    };

    /** 一个 tag 的状态（spec-pcie §10.3） */
    struct Tag
    {
        /** 正在使用 */
        bool inUse = false;
        /** 隔离中 */
        bool quarantined = false;
        /** 每次分配递增，回调据此识别 */
        uint64_t use = 0;
        /** 上次分配的时刻，按最久未用分配 */
        Tick lastUsed = 0;
        /** 完成超时的时刻 */
        Tick deadline = 0;
        /** 隔离期满的时刻 */
        Tick releaseAt = 0;
        /** 用途 */
        ReadKind kind = ReadKind::Data;
        /** 所属通道 */
        unsigned chan = 0;
        /** 所属描述符的编号 */
        uint64_t serial = 0;
        /** 请求的 host 地址 */
        Addr addr = 0;
        /** 请求的字节数 */
        unsigned len = 0;
        /** Data：数据的 DPA；Sgl：在段中的偏移 */
        Addr dest = 0;
    };

    /** 已到达、等待写入设备内存的 H2D 数据 */
    struct DevWrite
    {
        /** 所属通道 */
        unsigned chan = 0;
        /** 所属描述符的编号 */
        uint64_t serial = 0;
        /** DPA */
        Addr dpa = 0;
        /** 数据 */
        std::vector<uint8_t> data;
        /** 已写出的字节数 */
        unsigned done = 0;
    };

    /** 环的条目数 */
    static unsigned entries(const Channel &ch) { return 1u << ch.log2; }

    /** 描述符 idx 在环中的地址 */
    static Addr slotAddr(const Channel &ch, unsigned idx)
    { return ch.ringBase + Addr(idx) * 32; }

    /** 通道能否开始新的描述符 */
    bool canRun(const Channel &ch) const;

    /** 在通道的描述符中按编号查找；找不到返回 nullptr */
    Desc *findDesc(unsigned c, uint64_t serial);

    /** 读取描述符：最多预取 2 个 */
    void fetchDescs(unsigned c);

    /** 解析 DESC_RD 返回的 32 B */
    void parseDesc(unsigned c, Desc &d, const MlData &raw);

    /** 开始可以开始的描述符（校验字段，准备 SGL） */
    void startDescs(unsigned c);

    /** 描述符 d 出错：停止发出新请求，记录第一个错误 */
    void failDesc(unsigned c, Desc &d, HdmaErr err, Addr addr);

    /** 描述符是否已结束（可以回写） */
    static bool descDone(const Desc &d)
    { return d.started && d.issuedAll && d.outstanding == 0; }

    /** 回写已结束的描述符，按环的顺序 */
    void writeBack(unsigned c);

    /** 回写失败：通道立即进入错误停止（§8.2） */
    void writeBackFailed(unsigned c, unsigned idx);

    /** 已开始的描述符都结束后，进入错误停止（§8.2） */
    void maybeErrStop(unsigned c);

    /** 进入错误停止：写 CH_ERR，丢弃未开始的描述符，发出 HDMA_ERR */
    void enterErrStop(unsigned c);

    /** 推进中止：等在途操作排空，再逐个回写 ABORTED（§8.3） */
    void advanceAbort(unsigned c);

    /** 按 PAUSE、空闲更新通道状态 */
    void updateState(unsigned c);

    /** 本通道当前发请求的描述符（已开始、未发完）；没有返回 nullptr */
    Desc *issuingDesc(unsigned c);

    /**
     * 当前 host 地址段：成功时给出地址和这一段剩余的字节数；SGL 段尚未
     * 到达返回 false；SGL 非法时让描述符失败并返回 false
     */
    bool hostSpan(unsigned c, Desc &d, Addr &addr, uint64_t &left);

    /** host 地址前进 n 字节（只对 SGL 有意义） */
    void advanceHost(unsigned c, uint64_t n);

    /** 本通道的读方向是否有可以发出的请求；有则给出地址和长度 */
    bool readCandidate(unsigned c, ReadKind &kind, Addr &addr,
                       unsigned &len);

    /** 读方向：发出一个 MRd（SGL 段或 H2D 数据） */
    bool issueRead();

    /** 写方向：发出一个 MWr（D2H 数据） */
    bool issueWrite();

    /** D2H：发出一个设备内存读 */
    bool issueDevRead();

    /** H2D：把已到达的数据写入设备内存 */
    bool issueDevWrite();

    /**
     * 在 cand(c) 为真的通道中选一个，不改变轮询状态：通道 0 严格优先，
     * 其余加权轮询；last 是该方向的轮询位置，dir 是方向（0 读、1 写、
     * 2 设备内存读）。没有返回 -1
     */
    int arbitrate(const std::function<bool(unsigned)> &cand,
                  unsigned last, unsigned dir);

    /** 通道 c 在方向 dir 上发出了一个请求：更新轮询位置与配额 */
    void commitArb(unsigned c, unsigned &last, unsigned dir);

    /** 本通道的写方向是否有可以发出的 MWr；有则给出地址和长度 */
    bool writeCandidate(unsigned c, Addr &addr, unsigned &len);

    /** D2H 暂存缓冲中描述符 d 尚未写出的字节数 */
    static unsigned staged(const Desc &d);

    /** 从 D2H 暂存缓冲中取出描述符 d 的前 len 字节 */
    static void takeStaged(Desc &d, unsigned len, uint8_t *out);

    /** 开始中止通道 c（CH_CTRL.ABORT、HDMA_CTRL.ABORT_ALL） */
    void startAbort(unsigned c);

    /** 丢弃通道 c 尚未写入设备内存的 H2D 数据 */
    void dropDevWrites(unsigned c);

    /** 请求在不早于 when 的时钟沿 tick */
    void wakeAt(Tick when);

    /** 数据通路传送 len 字节所需的周期数 */
    Tick cycles(unsigned len) const;

    /** 分配一个 tag；没有可用的返回 -1 */
    int allocTag();

    /** 在途读请求数是否已达上限 */
    bool tagsFull() const;

    /** host 读请求完成 */
    void readDone(unsigned t, uint64_t use, HostStatus st,
                  const uint8_t *data);

    /** 处理完成超时与隔离期满 */
    void checkTags();

    /** 记录 host 读请求的错误：置 ERR_STATUS.DMA_HOST_ERR */
    void hostError();

    /** 把 SGL 段的已到达部分解析成条目：失败返回错误码 */
    HdmaErr nextSglEntry(unsigned c, Desc &d);

    /** 按上一次 tick 以来的停顿状态累计停顿周期 */
    void accountStalls(Tick now);

    /** 读通道寄存器 */
    bool readChannel(unsigned c, Addr off, uint32_t &v, bool mirror);

    /** 写通道寄存器 */
    bool writeChannel(unsigned c, Addr off, uint32_t v);

    /** 全部状态恢复复位值（tag 的隔离保留） */
    void clearState();

    /** 参数 */
    const Config cfg;
    /** 时钟 */
    HdmaClockIf &clk;
    /** host 内存 */
    HdmaHostIf &host;
    /** 设备内存 */
    HdmaDevIf &dev;
    /** ML */
    HdmaMlIf &ml;
    /** 事务参数 */
    PcieParams pcie;
    /** Bus Master Enable */
    bool busMaster = false;
    /** FLR 进行中，保持复位 */
    bool held = false;
    /** 每次复位递增；旧的回调据此丢弃 */
    uint64_t gen = 0;
    /** 下一个描述符编号 */
    uint64_t nextSerial = 1;
    /** HDMA_CTRL.EN */
    bool globalEn = false;
    /** 通道 */
    std::vector<Channel> chans;
    /** tag，按编号索引（256 个） */
    std::array<Tag, 256> tags{};
    /** 使用中的 tag 数 */
    unsigned tagsInUse = 0;
    /** 隔离中的 tag 数 */
    unsigned tagsQuarantined = 0;
    /** 完成缓冲已预留的字节数 */
    unsigned cplUsed = 0;
    /** D2H 暂存缓冲已预留的字节数 */
    unsigned d2hUsed = 0;
    /** 等待写入设备内存的 H2D 数据 */
    std::deque<DevWrite> devWrites;
    /** 待发出的事件位（链路未建立时保留） */
    uint32_t pendingEvents = 0;
    /** 读方向的轮询位置 */
    unsigned rrRead = 0;
    /** 写方向的轮询位置 */
    unsigned rrWrite = 0;
    /** 设备内存读的轮询位置 */
    unsigned rrDevRead = 0;
    /** 读方向下一次可以发请求的时刻 */
    Tick rdFreeAt = 0;
    /** 写方向下一次可以发请求的时刻 */
    Tick wrFreeAt = 0;
    /** 设备内存写通路下一次空闲的时刻 */
    Tick devWrFreeAt = 0;
    /** 设备内存读下一次可以发请求的时刻 */
    Tick devRdFreeAt = 0;
    /** 已请求的下一次 tick 的时刻；没有时为 MaxTick */
    Tick wakePending = MaxTick;
    /** 本次 tick 是否有进展 */
    bool progress = false;
    /** 本次 tick 中被数据通路节拍挡住的请求最早可以发出的时刻 */
    Tick soonest = MaxTick;
    /** 下一个 tag 超时或隔离期满的时刻 */
    Tick nextTagEvent = MaxTick;
    /** 本次 tick 中各类停顿是否成立 */
    std::array<bool, 4> stallNow{};
    /** HDMA_BYTES_H2D */
    uint64_t bytesH2d = 0;
    /** HDMA_BYTES_D2H */
    uint64_t bytesD2h = 0;
    /** 固件、镜像各自锁存的 HDMA_BYTES_H2D 高位 */
    std::array<uint32_t, 2> h2dHi{};
    /** 固件、镜像各自锁存的 HDMA_BYTES_D2H 高位 */
    std::array<uint32_t, 2> d2hHi{};
    /** 停顿计数：tag/完成缓冲、D2H 缓冲、NoC、发送 */
    std::array<uint32_t, 4> stall{};
    /** 上一次 tick 结束时各类停顿是否成立 */
    std::array<bool, 4> stalled{};
    /** 上一次累计停顿的时刻 */
    Tick stallAt = 0;
};

} // namespace gw100
} // namespace gem5

#endif // __GW100_PCIE_HDMA_HDMA_ENGINE_HH__
