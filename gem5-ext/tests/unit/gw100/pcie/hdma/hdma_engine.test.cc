/**
 * @file  hdma_engine.test.cc
 * @brief HdmaEngine 的单元测试
 *
 * 用假的时钟、host 内存、设备内存和 ML 驱动 HDMA，按 spec-host-dma 的
 * 验收标准检查：连续缓冲与 SGL 两种方式的数据、请求长度与 4 KiB 边界、
 * 未完成读请求数、完成的顺序与通知、各种错误码与错误停止、RESUME、
 * ABORT（含回写失败后的不回写中止）、完成超时与 tag 隔离、复位、
 * Bus Master Enable 清零、仲裁、BAR0 镜像，以及发送通路满时的停顿。
 *
 * @see spec-host-dma
 */

#include <gtest/gtest.h>

#include <fstream>

#include "base/gtest/serialization_fixture.hh"

#include <cstring>
#include <map>
#include <set>
#include <vector>

#include "src/gw100/pcie/hdma/hdma_engine.hh"

using namespace gem5;
using namespace gem5::gw100;

namespace
{

/** 1 ns 对应的 tick 数 */
constexpr Tick ns = 1000;
/** clk_pcie 的周期：250 MHz */
constexpr Tick Period = 4 * ns;
/** HDMA_NOTIFY 事件位 */
constexpr uint32_t EvtNotify = 1u << 5;
/** HDMA_ERR 事件位 */
constexpr uint32_t EvtErr = 1u << 6;

/** 事件驱动的小仿真器：按时刻执行回调 */
class Sim
{
  public:
    /** 当前时刻 */
    Tick now = 0;
    /** 待执行的回调 */
    std::multimap<Tick, std::function<void()>> q;

    /** 在 when 时刻执行 f */
    void at(Tick when, std::function<void()> f) { q.emplace(when, f); }

    /** 执行到队列为空或到达 until */
    void
    run(Tick until = MaxTick)
    {
        while (!q.empty() && q.begin()->first <= until) {
            auto it = q.begin();
            now = it->first;
            auto f = it->second;
            q.erase(it);
            f();
        }
        if (until != MaxTick && now < until)
            now = until;
    }
};

/** 假的时钟 */
class TestClock : public HdmaClockIf
{
  public:
    /** 构造 */
    explicit TestClock(Sim &sim) : sim(sim) {}
    /** 被驱动的 HDMA */
    HdmaEngine *eng = nullptr;
    /** 当前时刻 */
    Tick now() const override { return sim.now; }
    /** 周期 */
    Tick period() const override { return Period; }

    /** 在不早于 when 的时钟沿调用 tick() */
    void
    wakeAt(Tick when) override
    {
        Tick e = (std::max(when, sim.now) + Period - 1) / Period * Period;
        if (scheduled.count(e))
            return;
        scheduled.insert(e);
        sim.at(e, [this, e] {
            scheduled.erase(e);
            eng->tick();
        });
    }

  private:
    /** 仿真器 */
    Sim &sim;
    /** 已安排 tick 的时钟沿 */
    std::set<Tick> scheduled;
};

/** 假的 host 内存：检查请求的长度和 4 KiB 边界 */
class TestHost : public HdmaHostIf
{
  public:
    /** 构造 */
    explicit TestHost(Sim &sim) : sim(sim), mem(1 << 20, 0) {}
    /** host 内存，地址 0 起 */
    std::vector<uint8_t> mem;
    /** 读完成的延迟 */
    Tick latency = 500 * ns;
    /** 本地准入后到 DLL 提交的等待，单位 tick。 */
    Tick commitDelay = 0;
    /** 读这些地址范围时返回 UR */
    std::vector<std::pair<Addr, Addr>> urRanges;
    /** 读这些地址时不返回完成（制造超时） */
    std::set<Addr> dropReads;
    /** 被扣下的读的回调，按地址 */
    std::map<Addr, HostReadDone> dropped;
    /** 在途读请求数 */
    unsigned outstanding = 0;
    /** 在途读请求数的最大值 */
    unsigned maxOutstanding = 0;
    /** 读请求的最大长度上限（MRRS） */
    unsigned mrrs = 512;
    /** 写请求的最大长度上限（MPS） */
    unsigned mps = 128;
    /** 发送通路能否接受写 */
    bool txOk = true;
    /** 在途写请求的上限；0 表示不限 */
    unsigned txCredits = 0;
    /** 在途写请求数 */
    unsigned txInFlight = 0;
    /** 写请求完成的延迟 */
    Tick writeLatency = 300 * ns;
    /** 在途写请求减少时唤醒 HDMA */
    HdmaEngine *eng = nullptr;
    /** 读请求的个数 */
    unsigned reads = 0;
    /** 写请求的个数 */
    unsigned writes = 0;
    /** 违反长度或 4 KiB 边界的请求个数 */
    unsigned violations = 0;
    /** 各次读请求的地址，按发出顺序 */
    std::vector<Addr> readAddrs;
    /** 实际提交的请求 Attr[1:0]。 */
    std::vector<uint8_t> attributes;

    /** 检查一个请求 */
    void
    check(Addr a, unsigned len, unsigned max)
    {
        if (len == 0 || ((len + (a & 3) + 3) & ~3u) > max ||
            (a & 4095) + len > 4096) {
            violations++;
        }
    }

    /** 发出一个 MRd */
    void
    read(Addr a, unsigned len, HostReadDone done) override
    {
        check(a, len, mrrs);
        reads++;
        readAddrs.push_back(a);
        outstanding++;
        maxOutstanding = std::max(maxOutstanding, outstanding);
        if (dropReads.count(a)) {
            dropped[a] = done;
            return;
        }
        bool ur = false;
        for (auto &r : urRanges)
            ur = ur || (a >= r.first && a < r.second);
        sim.at(sim.now + latency, [this, a, len, done, ur] {
            outstanding--;
            if (ur)
                done(HostStatus::Ur, nullptr);
            else
                done(HostStatus::Ok, mem.data() + a);
            (void)len;
        });
    }

    /** 接受真实 tag 与 Attr[2:0]，在 DLL 提交时调用 committed。 */
    bool
    readTagged(Addr addr, unsigned len, uint8_t,
               std::function<void()> committed, HostReadDone done,
               uint8_t attributes = 0) override
    {
        this->attributes.push_back(attributes);
        if (!commitDelay) {
            committed();
            read(addr, len, std::move(done));
        } else {
            sim.at(sim.now + commitDelay, [this, addr, len, committed, done] {
                committed();
                read(addr, len, done);
            });
        }
        return true;
    }

    /** 发送通路能否再接受一个写请求 */
    bool
    canWrite() const override
    {
        return txOk && (txCredits == 0 || txInFlight < txCredits);
    }

    /** 发出一个 MWr */
    void
    write(Addr a, unsigned len, const uint8_t *data,
          uint8_t attributes = 0) override
    {
        this->attributes.push_back(attributes);
        check(a, len, mps);
        writes++;
        if (txCredits == 0) {
            std::memcpy(mem.data() + a, data, len);
            return;
        }
        txInFlight++;
        std::vector<uint8_t> copy(data, data + len);
        sim.at(sim.now + writeLatency, [this, a, copy] {
            std::memcpy(mem.data() + a, copy.data(), copy.size());
            txInFlight--;
            eng->wake();
        });
    }

  private:
    /** 仿真器 */
    Sim &sim;
};

/** 假的设备内存 */
class TestDev : public HdmaDevIf
{
  public:
    /** 构造 */
    explicit TestDev(Sim &sim) : sim(sim), mem(1 << 20, 0) {}
    /** 设备内存，DPA 0 起 */
    std::vector<uint8_t> mem;
    /** 访问延迟 */
    Tick latency = 60 * ns;

    /** 总能接受 */
    bool canSend() const override { return true; }

    /** 读 */
    void
    read(Addr dpa, unsigned len, DevReadDone done) override
    {
        sim.at(sim.now + latency, [this, dpa, len, done] {
            done(true, mem.data() + dpa);
            (void)len;
        });
    }

    /** 写 */
    void
    write(Addr dpa, unsigned len, const uint8_t *data,
          DevWriteDone done) override
    {
        std::vector<uint8_t> copy(data, data + len);
        sim.at(sim.now + latency, [this, dpa, copy, done] {
            std::memcpy(mem.data() + dpa, copy.data(), copy.size());
            done(true);
        });
    }

  private:
    /** 仿真器 */
    Sim &sim;
};

/** 假的 ML：描述符环放在一块"管理 Tile SRAM"中 */
class TestMl : public HdmaMlIf
{
  public:
    /** 构造 */
    explicit TestMl(Sim &sim) : sim(sim), sram(64 << 10, 0) {}
    /** 管理 Tile 的 SRAM，本地地址 SramBase 起 */
    std::vector<uint8_t> sram;
    /** SRAM 的本地地址 */
    static constexpr Addr SramBase = 0x1000'0000;
    /** 单程延迟 */
    Tick latency = 100 * ns;
    /** 读这些地址时返回 REJECT */
    std::set<Addr> rejectRead;
    /** 回写这些地址时返回 REJECT */
    std::set<Addr> rejectWrite;
    /** 收到的事件，按到达顺序 */
    std::vector<uint32_t> events;
    /** 每个事件到达时，各描述符 DW7 的快照（检查通知在回写之后） */
    std::vector<std::vector<uint8_t>> sramAtEvent;
    /** 撤销次数 */
    unsigned withdrawals = 0;

    /** 读 32 B 的描述符 */
    bool
    descRead(Addr a, DescReadDone done) override
    {
        bool rej = rejectRead.count(a);
        sim.at(sim.now + latency, [this, a, done, rej] {
            MlData d{};
            if (!rej)
                std::memcpy(d.data(), sram.data() + (a - SramBase), 32);
            done(rej ? MlStatus::Reject : MlStatus::Ok, d);
        });
        return true;
    }

    /** 回写 DW7 */
    bool
    descWrite(Addr a, uint32_t dw7, DescWriteDone done) override
    {
        bool rej = rejectWrite.count(a);
        sim.at(sim.now + latency, [this, a, dw7, done, rej] {
            if (!rej)
                std::memcpy(sram.data() + (a - SramBase) + 28, &dw7, 4);
            done(rej ? MlStatus::Reject : MlStatus::Ok);
        });
        return true;
    }

    /** posted 事件 */
    bool
    event(uint32_t bits) override
    {
        sim.at(sim.now + latency, [this, bits] {
            events.push_back(bits);
            sramAtEvent.push_back(sram);
        });
        return true;
    }

    /** 撤销 */
    void withdraw() override { withdrawals++; }

  private:
    /** 仿真器 */
    Sim &sim;
};

/** 一个描述符的字段 */
struct DescSpec
{
    /** TYPE */
    uint8_t type = 1;
    /** HOST_SGL */
    bool sgl = false;
    /** NOTIFY */
    bool notify = false;
    /** FENCE */
    bool fence = false;
    /** TAG */
    uint16_t tag = 0;
    /** LENGTH */
    uint32_t len = 0;
    /** HOST_ADDR */
    uint64_t host = 0;
    /** DEV_ADDR */
    uint64_t dev = 0;
    /** SGL_SEG_BYTES */
    uint16_t segBytes = 0;
    /** No Snoop 与 RO 请求位。 */
    uint8_t attributes = 0;
};

/** 测试台：仿真器、各假接口和被测的 HDMA */
class Rig
{
  public:
    /** 仿真器 */
    Sim sim;
    /** 时钟 */
    TestClock clk{sim};
    /** host 内存 */
    TestHost host{sim};
    /** 设备内存 */
    TestDev dev{sim};
    /** ML */
    TestMl ml{sim};
    /** host 读请求出错的次数 */
    unsigned hostErrors = 0;
    /** 被测的 HDMA */
    std::unique_ptr<HdmaEngine> eng;

    /** 构造并使能 HDMA */
    Rig()
    {
        HdmaEngine::Config cfg;
        cfg.devBase = 0;
        cfg.devSize = 1 << 20;
        cfg.cplTimeout = 50 * 1000 * ns;
        cfg.quarantine = 250 * 1000 * ns;
        eng = std::make_unique<HdmaEngine>(cfg, clk, host, dev, ml);
        clk.eng = eng.get();
        host.eng = eng.get();
        eng->onHostError = [this] { hostErrors++; };
        HdmaEngine::PcieParams pp;
        pp.mps = host.mps;
        pp.mrrs = host.mrrs;
        eng->setPcie(pp);
        eng->setBusMaster(true);
        wr(0x00C, 1);
    }

    /** 写寄存器 */
    void wr(Addr off, uint32_t v) { ASSERT_TRUE(eng->write(off, v)); }

    /** 读寄存器（固件视角） */
    uint32_t
    rd(Addr off)
    {
        uint32_t v = 0;
        EXPECT_TRUE(eng->read(off, v, false));
        return v;
    }

    /** 通道 c 的寄存器偏移 */
    static Addr ch(unsigned c, Addr off) { return 0x100 + 0x40 * c + off; }

    /** 通道 c 的环基地址（管理 Tile SRAM 中） */
    static Addr ring(unsigned c) { return TestMl::SramBase + 0x1000 * c; }

    /** 配置通道 c：环有 2^log2 个条目，权重 w，使能 */
    void
    setup(unsigned c, unsigned log2 = 4, unsigned w = 1)
    {
        wr(ch(c, 0x08), uint32_t(ring(c)));
        wr(ch(c, 0x0C), uint32_t(ring(c) >> 32));
        wr(ch(c, 0x10), log2);
        wr(ch(c, 0x00), 1u | (w << 8));
    }

    /** 把描述符写入通道 c 的第 idx 项（DW7 清零） */
    void
    putDesc(unsigned c, unsigned idx, const DescSpec &d)
    {
        uint32_t w[8] = {};
        w[0] = d.type | (d.sgl << 8) | (d.notify << 11) | (d.fence << 12) |
               (uint32_t(d.tag) << 16) | (uint32_t(d.attributes) << 9);
        w[1] = d.len;
        w[2] = uint32_t(d.host);
        w[3] = uint32_t(d.host >> 32);
        w[4] = uint32_t(d.dev);
        w[5] = uint32_t(d.dev >> 32);
        w[6] = d.segBytes;
        std::memcpy(ml.sram.data() + (ring(c) - TestMl::SramBase) + 32 * idx,
                    w, 32);
    }

    /** 通道 c 第 idx 项的 DW7 */
    uint32_t
    dw7(unsigned c, unsigned idx)
    {
        uint32_t v;
        std::memcpy(&v, ml.sram.data() + (ring(c) - TestMl::SramBase) +
                        32 * idx + 28, 4);
        return v;
    }

    /** 在 host 内存 a 起写一个 SGL 条目 */
    void
    putSgl(Addr a, uint64_t addr, uint32_t len, uint8_t type)
    {
        std::memcpy(host.mem.data() + a, &addr, 8);
        std::memcpy(host.mem.data() + a + 8, &len, 4);
        host.mem[a + 12] = type;
        host.mem[a + 13] = host.mem[a + 14] = host.mem[a + 15] = 0;
    }

    /** 提交：写 TAIL 并运行到静止 */
    void
    submit(unsigned c, unsigned tail)
    {
        wr(ch(c, 0x14), tail);
        sim.run();
    }

    /** 通道 c 的状态 */
    unsigned state(unsigned c) { return rd(ch(c, 0x04)) & 3; }
};

/** 用可辨认的字节填充 v 的 [off, off + n) */
void
fill(std::vector<uint8_t> &v, Addr off, unsigned n, uint8_t seed)
{
    for (unsigned i = 0; i < n; i++)
        v[off + i] = uint8_t(seed + i * 7 + (i >> 8));
}

/** v 与 w 的指定区间是否相同 */
bool
same(const std::vector<uint8_t> &v, Addr a, const std::vector<uint8_t> &w,
     Addr b, unsigned n)
{
    return std::memcmp(v.data() + a, w.data() + b, n) == 0;
}

} // anonymous namespace

TEST(HdmaEngine, RegistersAfterReset)
{
    Rig r;
    EXPECT_EQ(r.rd(0x000), 0x0100u);
    EXPECT_EQ(r.rd(0x004), 8u | (8u << 8));
    EXPECT_EQ(r.rd(0x00C), 1u);
    EXPECT_EQ(r.rd(Rig::ch(3, 0x04)), 1u << 8);   // 空闲、环空
    uint32_t v;
    EXPECT_FALSE(r.eng->read(0x040, v, false));   // 未实现
    EXPECT_FALSE(r.eng->read(0x100 + 0x40 * 8, v, false));
}

TEST(HdmaEngine, H2dContiguous)
{
    Rig r;
    r.setup(1);
    fill(r.host.mem, 0x1004, 12288, 3);   // 4 B 对齐、跨 4 KiB
    r.putDesc(1, 0, {1, false, false, false, 0x55, 12288, 0x1004, 0x8000});
    r.submit(1, 1);
    EXPECT_TRUE(same(r.dev.mem, 0x8000, r.host.mem, 0x1004, 12288));
    EXPECT_EQ(r.dw7(1, 0), 1u);
    EXPECT_EQ(r.rd(Rig::ch(1, 0x18)), 1u);     // HEAD
    EXPECT_EQ(r.rd(Rig::ch(1, 0x28)), 1u);     // DESC_DONE
    EXPECT_EQ(r.rd(Rig::ch(1, 0x30)), 12288u);
    EXPECT_EQ(r.rd(0x010), 12288u);
    EXPECT_EQ(r.state(1), 0u);
    EXPECT_EQ(r.host.violations, 0u);
    EXPECT_LE(r.host.maxOutstanding, 64u);
}

TEST(HdmaEngine, D2hContiguous)
{
    Rig r;
    r.setup(2);
    fill(r.dev.mem, 0x2000, 9000, 9);
    r.putDesc(2, 0, {2, false, false, false, 0, 9000, 0x30FC, 0x2000});
    r.submit(2, 1);
    EXPECT_TRUE(same(r.host.mem, 0x30FC, r.dev.mem, 0x2000, 9000));
    EXPECT_EQ(r.dw7(2, 0), 1u);
    EXPECT_EQ(r.rd(0x018), 9000u);
    EXPECT_EQ(r.host.violations, 0u);
}

TEST(HdmaEngine, RequestSizesFollowMpsMrrs)
{
    for (unsigned mrrs : {128u, 256u, 4096u}) {
        for (unsigned mps : {128u, 256u}) {
            Rig r;
            r.host.mrrs = std::min(mrrs, 512u);
            r.host.mps = mps;
            HdmaEngine::PcieParams pp;
            pp.mrrs = std::min(mrrs, 512u);
            pp.mps = mps;
            r.eng->setPcie(pp);
            r.setup(1);
            fill(r.host.mem, 0x0, 20000, 1);
            r.putDesc(1, 0, {1, false, false, false, 0, 20000, 0x0, 0x0});
            // FENCE：等 H2D 写完设备内存再读回
            r.putDesc(1, 1, {2, false, false, true, 0, 20000, 0x40000,
                             0x0});
            r.submit(1, 2);
            EXPECT_EQ(r.host.violations, 0u) << mrrs << " " << mps;
            EXPECT_TRUE(same(r.host.mem, 0x40000, r.host.mem, 0x0, 20000));
            EXPECT_LE(r.host.maxOutstanding, 64u);
        }
    }
}

TEST(HdmaEngine, OutstandingReadsBoundedByTags)
{
    Rig r;
    r.host.latency = 20000 * ns;   // 很长的读延迟：所有 tag 都会用上
    r.setup(1);
    r.putDesc(1, 0, {1, false, false, false, 0, 256 << 10, 0x0, 0x0});
    r.submit(1, 1);
    EXPECT_EQ(r.host.maxOutstanding, 64u);
    EXPECT_GT(r.rd(0x020), 0u);    // HDMA_STALL_TAG
    EXPECT_EQ(r.dw7(1, 0), 1u);
}

TEST(HdmaEngine, SglH2dTwoSegments)
{
    Rig r;
    r.setup(1);
    // 第一段：两个 DATA 加一个 SEGMENT；第二段：一个 DATA
    r.putSgl(0x80000, 0x10000, 4096, 0);
    r.putSgl(0x80010, 0x20004, 1000, 0);
    r.putSgl(0x80020, 0x90000, 16, 1);
    r.putSgl(0x90000, 0x30000, 3000, 0);
    fill(r.host.mem, 0x10000, 4096, 1);
    fill(r.host.mem, 0x20004, 1000, 2);
    fill(r.host.mem, 0x30000, 3000, 3);
    r.putDesc(1, 0, {1, true, false, false, 0, 8096, 0x80000, 0x4000, 48});
    r.submit(1, 1);
    EXPECT_EQ(r.dw7(1, 0), 1u);
    EXPECT_TRUE(same(r.dev.mem, 0x4000, r.host.mem, 0x10000, 4096));
    EXPECT_TRUE(same(r.dev.mem, 0x5000, r.host.mem, 0x20004, 1000));
    EXPECT_TRUE(same(r.dev.mem, 0x53E8, r.host.mem, 0x30000, 3000));
}

TEST(HdmaEngine, SglD2hThreePages)
{
    Rig r;
    r.setup(1);
    r.putSgl(0x80000, 0x10000, 4096, 0);
    r.putSgl(0x80010, 0x30000, 4096, 0);
    r.putSgl(0x80020, 0x20000, 4096, 0);
    fill(r.dev.mem, 0x0, 12288, 5);
    r.putDesc(1, 0, {2, true, false, false, 0, 12288, 0x80000, 0x0, 48});
    r.submit(1, 1);
    EXPECT_EQ(r.dw7(1, 0), 1u);
    EXPECT_TRUE(same(r.host.mem, 0x10000, r.dev.mem, 0x0, 4096));
    EXPECT_TRUE(same(r.host.mem, 0x30000, r.dev.mem, 0x1000, 4096));
    EXPECT_TRUE(same(r.host.mem, 0x20000, r.dev.mem, 0x2000, 4096));
    EXPECT_EQ(r.host.violations, 0u);
}

TEST(HdmaEngine, SglInvalidStopsChannelAndResume)
{
    Rig r;
    r.setup(1);
    // SEGMENT 不是段的最后一个条目（S2）
    r.putSgl(0x80000, 0x90000, 16, 1);
    r.putSgl(0x80010, 0x10000, 64, 0);
    r.putDesc(1, 0, {1, true, false, false, 0, 64, 0x80000, 0x0, 32});
    r.putDesc(1, 1, {1, false, false, false, 0, 64, 0x10000, 0x100});
    r.submit(1, 2);
    EXPECT_EQ(r.dw7(1, 0), 1u | (8u << 8));    // SGL_INVALID
    EXPECT_EQ(r.state(1), 3u);
    uint32_t err = r.rd(Rig::ch(1, 0x1C));
    EXPECT_EQ(err & 0xFF, 8u);
    EXPECT_EQ((err >> 8) & 3, 0u);              // PHASE = 数据传输
    EXPECT_EQ(err >> 16, 0u);
    EXPECT_EQ(r.rd(Rig::ch(1, 0x18)), 1u);      // HEAD 指向未开始的项
    ASSERT_FALSE(r.ml.events.empty());
    EXPECT_TRUE(r.ml.events.back() & EvtErr);
    // RESUME：从 HEAD 继续；CH_ERR 保持
    r.wr(Rig::ch(1, 0x00), 1u | (1u << 8) | (1u << 3));
    r.sim.run();
    EXPECT_EQ(r.dw7(1, 1), 1u);
    EXPECT_EQ(r.state(1), 0u);
    EXPECT_EQ(r.rd(Rig::ch(1, 0x1C)), err);
}

TEST(HdmaEngine, DescInvalidAndDevAddr)
{
    Rig r;
    r.setup(1);
    r.putDesc(1, 0, {1, false, false, false, 0, 62, 0x0, 0x0});  // 非 4 倍
    r.submit(1, 1);
    EXPECT_EQ(r.dw7(1, 0), 1u | (7u << 8));
    r.wr(Rig::ch(1, 0x00), 1u | (1u << 8) | (1u << 3));
    r.putDesc(1, 1, {1, false, false, false, 0, 64, 0x0, 0xFFFFF0});
    r.submit(1, 2);
    EXPECT_EQ(r.dw7(1, 1), 1u | (5u << 8));     // DEV_ADDR
    EXPECT_EQ(r.rd(Rig::ch(1, 0x20)), 0xFFFFF0u);
    EXPECT_EQ(r.host.reads, 0u);                // 没有访问 host
}

TEST(HdmaEngine, Channel0RejectsAttributes)
{
    Rig r;
    r.setup(0);
    DescSpec d{1, false, false, false, 0, 64, 0x0, 0x0};
    r.putDesc(0, 0, d);
    // NO_SNOOP 置位
    uint32_t w0;
    Addr p = Rig::ring(0) - TestMl::SramBase;
    std::memcpy(&w0, r.ml.sram.data() + p, 4);
    w0 |= 1u << 9;
    std::memcpy(r.ml.sram.data() + p, &w0, 4);
    r.submit(0, 1);
    EXPECT_EQ(r.dw7(0, 0), 1u | (7u << 8));
}

TEST(HdmaEngine, HostUrReportsError)
{
    Rig r;
    r.setup(1);
    r.host.urRanges.push_back({0x5000, 0x6000});
    r.putDesc(1, 0, {1, false, false, false, 0, 8192, 0x4000, 0x0});
    r.submit(1, 1);
    EXPECT_EQ(r.dw7(1, 0), 1u | (1u << 8));     // HOST_UR
    EXPECT_GE(r.hostErrors, 1u);
    EXPECT_EQ(r.rd(Rig::ch(1, 0x20)), 0x5000u);
    EXPECT_EQ(r.state(1), 3u);
}

TEST(HdmaEngine, InOrderCompletionNotifyAfterWriteback)
{
    Rig r;
    r.setup(1);
    r.putDesc(1, 0, {1, false, false, false, 1, 8192, 0x0, 0x0});
    r.putDesc(1, 1, {1, false, true, false, 2, 64, 0x10000, 0x8000});
    r.putDesc(1, 2, {1, false, true, true, 3, 64, 0x20000, 0x9000});
    r.submit(1, 3);
    for (unsigned i = 0; i < 3; i++)
        EXPECT_EQ(r.dw7(1, i), 1u);
    // 每个事件到达时，触发它的描述符的 DW7 已写好
    ASSERT_FALSE(r.ml.events.empty());
    for (size_t k = 0; k < r.ml.events.size(); k++) {
        EXPECT_TRUE(r.ml.events[k] & EvtNotify);
        uint32_t v;
        std::memcpy(&v, r.ml.sramAtEvent[k].data() +
                        (Rig::ring(1) - TestMl::SramBase) + 32 + 28, 4);
        EXPECT_EQ(v, 1u);
    }
    // NOTIFY = 0 的描述符不产生事件：最多两次（可能合并）
    EXPECT_LE(r.ml.events.size(), 2u);
}

TEST(HdmaEngine, AbortPendingDescriptors)
{
    Rig r;
    r.setup(1);
    for (unsigned i = 0; i < 3; i++)
        r.putDesc(1, i, {1, false, false, false, 0, 64, 0x0, 0x0});
    r.wr(Rig::ch(1, 0x00), 0);                  // EN = 0：不处理
    r.wr(Rig::ch(1, 0x14), 3);
    r.sim.run();
    EXPECT_EQ(r.rd(Rig::ch(1, 0x18)), 0u);
    r.wr(Rig::ch(1, 0x00), 1u << 2);            // ABORT，EN 保持 0
    EXPECT_EQ(r.state(1), 1u);                  // 中止进行中
    r.sim.run();
    for (unsigned i = 0; i < 3; i++)
        EXPECT_EQ(r.dw7(1, i), 1u | (9u << 8)); // ABORTED
    EXPECT_EQ(r.rd(Rig::ch(1, 0x18)), 3u);
    EXPECT_EQ(r.state(1), 0u);
    EXPECT_TRUE(r.ml.events.empty());           // 中止不发通知
    // 空闲时再写 ABORT 没有效果
    r.wr(Rig::ch(1, 0x00), 1u << 2);
    EXPECT_EQ(r.state(1), 0u);
}

TEST(HdmaEngine, WritebackFailureAndSilentAbort)
{
    Rig r;
    r.setup(1);
    r.putDesc(1, 0, {1, false, false, false, 0, 64, 0x0, 0x0});
    r.putDesc(1, 1, {1, false, false, false, 0, 64, 0x100, 0x100});
    r.putDesc(1, 2, {1, false, false, false, 0, 64, 0x200, 0x200});
    r.ml.rejectWrite.insert(Rig::ring(1) + 32);
    r.submit(1, 3);
    EXPECT_EQ(r.dw7(1, 0), 1u);
    EXPECT_EQ(r.dw7(1, 1), 0u);                 // 回写失败
    EXPECT_EQ(r.state(1), 3u);
    uint32_t err = r.rd(Rig::ch(1, 0x1C));
    EXPECT_EQ(err & 0xFF, 5u);
    EXPECT_EQ((err >> 8) & 3, 2u);              // PHASE = 描述符回写
    EXPECT_EQ(r.rd(Rig::ch(1, 0x18)), 1u);      // HEAD 不推进
    // RESUME 无效
    r.wr(Rig::ch(1, 0x00), 1u | (1u << 8) | (1u << 3));
    r.sim.run();
    EXPECT_EQ(r.state(1), 3u);
    // 不回写的 ABORT：HEAD 不变，TAIL 被置为 HEAD
    r.wr(Rig::ch(1, 0x00), 1u | (1u << 8) | (1u << 2));
    r.sim.run();
    EXPECT_EQ(r.state(1), 0u);
    EXPECT_EQ(r.rd(Rig::ch(1, 0x18)), 1u);
    EXPECT_EQ(r.rd(Rig::ch(1, 0x14)), 1u);
    EXPECT_EQ(r.dw7(1, 2), 0u);
    // 从 HEAD 起重新提交可以继续使用
    r.ml.rejectWrite.clear();
    r.putDesc(1, 1, {1, false, false, false, 0, 64, 0x300, 0x300});
    r.submit(1, 2);
    EXPECT_EQ(r.dw7(1, 1), 1u);
}

TEST(HdmaEngine, DescriptorReadFailureThenResume)
{
    Rig r;
    r.setup(1);
    r.putDesc(1, 0, {1, false, false, false, 0, 64, 0x0, 0x0});
    r.putDesc(1, 1, {1, false, false, false, 0, 64, 0x100, 0x100});
    r.ml.rejectRead.insert(Rig::ring(1) + 32);
    r.submit(1, 2);
    EXPECT_EQ(r.dw7(1, 0), 1u);                 // 前面的照常完成
    EXPECT_EQ(r.state(1), 3u);
    uint32_t err = r.rd(Rig::ch(1, 0x1C));
    EXPECT_EQ(err & 0xFF, 5u);
    EXPECT_EQ((err >> 8) & 3, 1u);              // PHASE = 描述符读取
    EXPECT_EQ(err >> 16, 1u);
    EXPECT_EQ(r.rd(Rig::ch(1, 0x20)), uint32_t(Rig::ring(1) + 32));
    EXPECT_EQ(r.rd(Rig::ch(1, 0x18)), 1u);
    r.ml.rejectRead.clear();
    r.wr(Rig::ch(1, 0x00), 1u | (1u << 8) | (1u << 3));
    r.sim.run();
    EXPECT_EQ(r.dw7(1, 1), 1u);
    EXPECT_EQ(r.state(1), 0u);
}

TEST(HdmaEngine, CompletionTimeoutQuarantinesTag)
{
    Rig r;
    r.setup(1);
    r.host.dropReads.insert(0x0);
    r.putDesc(1, 0, {1, false, false, false, 0, 64, 0x0, 0x0});
    r.submit(1, 1);
    EXPECT_EQ(r.dw7(1, 0), 1u | (3u << 8));     // HOST_TIMEOUT
    EXPECT_GE(r.sim.now, 50 * 1000 * ns);
    EXPECT_GE(r.hostErrors, 1u);
    // 迟到的完成被丢弃，不影响后来的传输
    ASSERT_TRUE(r.host.dropped.count(0x0));
    r.host.dropped[0x0](HostStatus::Ok, r.host.mem.data());
    r.wr(Rig::ch(1, 0x00), 1u | (1u << 8) | (1u << 3));
    fill(r.host.mem, 0x1000, 64, 4);
    r.putDesc(1, 1, {1, false, false, false, 0, 64, 0x1000, 0x40});
    r.submit(1, 2);
    EXPECT_EQ(r.dw7(1, 1), 1u);
    EXPECT_TRUE(same(r.dev.mem, 0x40, r.host.mem, 0x1000, 64));
}

TEST(HdmaEngine, ResetClearsRegistersAndWithdraws)
{
    Rig r;
    r.setup(1);
    r.putDesc(1, 0, {1, false, false, false, 0, 4096, 0x0, 0x0});
    r.wr(Rig::ch(1, 0x14), 1);
    r.sim.run(r.sim.now + 300 * ns);            // 传输进行中
    r.eng->reset();
    EXPECT_EQ(r.ml.withdrawals, 1u);
    EXPECT_EQ(r.rd(0x00C), 0u);
    EXPECT_EQ(r.rd(Rig::ch(1, 0x00)), 0u);
    EXPECT_EQ(r.rd(Rig::ch(1, 0x18)), 0u);
    r.sim.run();                                // 旧的完成到达后被丢弃
    EXPECT_EQ(r.dw7(1, 0), 0u);
    EXPECT_EQ(r.rd(Rig::ch(1, 0x28)), 0u);
}

TEST(HdmaEngine, HoldIgnoresWrites)
{
    Rig r;
    r.eng->reset();
    r.eng->setHold(true);
    r.wr(0x00C, 1);
    EXPECT_EQ(r.rd(0x00C), 0u);
    r.eng->setHold(false);
    r.wr(0x00C, 1);
    EXPECT_EQ(r.rd(0x00C), 1u);
}

TEST(HdmaEngine, BusMasterOffFailsActive)
{
    Rig r;
    r.host.latency = 2000 * ns;
    r.setup(1);
    r.putDesc(1, 0, {1, false, false, false, 0, 64 << 10, 0x0, 0x0});
    r.wr(Rig::ch(1, 0x14), 1);
    r.sim.run(r.sim.now + 1000 * ns);
    r.eng->setBusMaster(false);
    unsigned reads = r.host.reads;
    r.sim.run();
    EXPECT_EQ(r.dw7(1, 0), 1u | (10u << 8));    // BUS_MASTER_OFF
    EXPECT_EQ(r.host.reads, reads);             // 不再发新请求
    EXPECT_EQ(r.state(1), 3u);
}

TEST(HdmaEngine, Channel0StrictPriority)
{
    Rig r;
    r.host.latency = 4000 * ns;
    r.setup(0);
    r.setup(1);
    r.putDesc(1, 0, {1, false, false, false, 0, 32 << 10, 0x0, 0x0});
    r.wr(Rig::ch(1, 0x14), 1);
    r.sim.run(r.sim.now + 100 * ns);
    r.putDesc(0, 0, {1, false, false, false, 0, 2048, 0x40000, 0x40000});
    size_t before = r.host.readAddrs.size();
    r.wr(Rig::ch(0, 0x14), 1);
    r.sim.run();
    // 通道 0 的 4 个读请求在通道 1 的下一个请求之前全部发出
    unsigned ch0 = 0;
    bool interleaved = false;
    for (size_t i = before; i < r.host.readAddrs.size() && ch0 < 4; i++) {
        if (r.host.readAddrs[i] >= 0x40000)
            ch0++;
        else if (ch0 > 0)
            interleaved = true;
    }
    EXPECT_EQ(ch0, 4u);
    EXPECT_FALSE(interleaved);
}

TEST(HdmaEngine, WeightedRoundRobin)
{
    Rig r;
    r.host.latency = 100 * ns;
    r.setup(1, 4, 1);
    r.setup(2, 4, 3);
    r.putDesc(1, 0, {1, false, false, false, 0, 64 << 10, 0x0, 0x0});
    r.putDesc(2, 0, {1, false, false, false, 0, 64 << 10, 0x40000,
                     0x40000});
    r.wr(Rig::ch(1, 0x14), 1);
    r.wr(Rig::ch(2, 0x14), 1);
    r.sim.run();
    // 两个通道都在发请求的那一段里，读请求的比例约为 1 : 3
    unsigned a = 0, b = 0;
    for (size_t i = 0; i < 64; i++)
        (r.host.readAddrs[i] >= 0x40000 ? b : a)++;
    EXPECT_NEAR(double(b) / a, 3.0, 0.5);
}

TEST(HdmaEngine, MirrorLatchIsIndependent)
{
    Rig r;
    r.setup(1);
    r.putDesc(1, 0, {1, false, false, false, 0, 4096, 0x0, 0x0});
    r.submit(1, 1);
    uint32_t v;
    ASSERT_TRUE(r.eng->read(0x010, v, true));   // 镜像读低位
    EXPECT_EQ(v, 4096u);
    r.putDesc(1, 1, {1, false, false, false, 0, 4096, 0x0, 0x0});
    r.submit(1, 2);
    EXPECT_EQ(r.rd(0x010), 8192u);              // 固件读低位
    ASSERT_TRUE(r.eng->read(0x014, v, true));
    EXPECT_EQ(v, 0u);
}

TEST(HdmaEngine, D2hWithTxCredits)
{
    Rig r;
    r.host.txCredits = 16;
    r.host.writeLatency = 2000 * ns;    // 写确认很慢：发送通路会满
    r.setup(1);
    r.putSgl(0x80000, 0x33000, 4096, 0);
    r.putSgl(0x80010, 0x31000, 4096, 0);
    r.putSgl(0x80020, 0x35000, 4096, 0);
    fill(r.dev.mem, 0x100000 - 0x10000, 12288, 5);
    r.putDesc(1, 0, {2, false, false, false, 0, 12288, 0x10004,
                     0xF0000});
    r.putDesc(1, 1, {2, true, false, true, 0, 12288, 0x80000, 0xF0000,
                     48});
    r.submit(1, 2);
    EXPECT_EQ(r.dw7(1, 0), 1u);
    EXPECT_EQ(r.dw7(1, 1), 1u);
    EXPECT_TRUE(same(r.host.mem, 0x10004, r.dev.mem, 0xF0000, 12288));
    EXPECT_TRUE(same(r.host.mem, 0x33000, r.dev.mem, 0xF0000, 4096));
    EXPECT_TRUE(same(r.host.mem, 0x31000, r.dev.mem, 0xF1000, 4096));
    EXPECT_TRUE(same(r.host.mem, 0x35000, r.dev.mem, 0xF2000, 4096));
    EXPECT_GT(r.rd(0x02C), 0u);    // HDMA_STALL_TX
}

/** 本地排队超过 completion timeout 仍不计时，DLL 提交后才开始。 */
TEST(HdmaEngine, CompletionTimeoutStartsAtDllCommit)
{
    Rig r;
    r.host.commitDelay = 60000 * ns;
    r.setup(1);
    r.putDesc(1, 0, {1, false, false, false, 0, 64, 0x1000, 0x1000});
    r.submit(1, 1);
    EXPECT_EQ(r.dw7(1, 0), 1u);
    EXPECT_EQ(r.hostErrors, 0);
}

/** 描述符的 Host 地址要求 DWORD 对齐，非法地址不能进入线上。 */
TEST(HdmaEngine, RejectUnalignedDescriptorBeforeWire)
{
    Rig r;
    r.host.mrrs = 128;
    r.host.mps = 128;
    HdmaEngine::PcieParams params;
    params.mrrs = 128;
    params.mps = 128;
    r.eng->setPcie(params);
    r.setup(1);
    r.putDesc(1, 0, {1, false, false, false, 0, 1024, 0x1001, 0x1000});
    r.submit(1, 1);
    EXPECT_EQ(r.dw7(1, 0), 1u | (7u << 8));
    EXPECT_EQ(r.host.reads, 0);
    EXPECT_EQ(r.host.writes, 0);
    EXPECT_EQ(r.host.violations, 0);
}

/** 数据描述符的 RO/No Snoop 受 Device Control 许可后进入线上。 */
TEST(HdmaEngine, DescriptorAttributesRespectDeviceControl)
{
    for (uint8_t enabled : {0, 1, 2, 3}) {
        for (uint8_t type : {1, 2}) {
            Rig rig;
            HdmaEngine::PcieParams params;
            params.noSnoop = enabled & 1;
            params.relaxed = enabled & 2;
            rig.eng->setPcie(params);
            rig.setup(1);
            DescSpec descriptor;
            descriptor.type = type;
            descriptor.len = 64;
            descriptor.host = 0x1000;
            descriptor.dev = 0x1000;
            descriptor.attributes = 3;
            rig.putDesc(1, 0, descriptor);
            rig.submit(1, 1);
            EXPECT_EQ(rig.dw7(1, 0), 1u);
            ASSERT_FALSE(rig.host.attributes.empty());
            for (auto attributes : rig.host.attributes) {
                EXPECT_EQ(attributes, enabled);
            }
        }
    }
}

/** 原生 INI 编解码夹具。 */
class HdmaSnapshotTest : public SerializationFixture
{
};

/** 父字段和各通道子区段均能从实际 INI 文件恢复。 */

/** 软件选择的完成超时在 DLL 提交后实际生效。 */
TEST(HdmaEngine, ProgrammableCompletionTimeout)
{
    Rig rig;
    HdmaEngine::PcieParams params;
    params.cplTimeout = 1000 * ns;
    rig.eng->setPcie(params);
    rig.setup(1);
    rig.host.dropReads.insert(0);
    rig.putDesc(1, 0, {1, false, false, false, 0, 64, 0, 0});
    rig.wr(Rig::ch(1, 0x14), 1);
    rig.sim.run(2000 * ns);
    EXPECT_EQ(rig.dw7(1, 0), 1u | (3u << 8));
}
