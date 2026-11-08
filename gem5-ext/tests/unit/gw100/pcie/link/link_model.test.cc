/**
 * @file  link_model.test.cc
 * @brief 双向链路训练、时序、反压和故障恢复的有界事件夹具
 *
 * @see spec-pcie
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <functional>
#include <set>
#include <stdexcept>
#include <vector>

#include "src/gw100/pcie/link/link_model.hh"

using namespace gem5::gw100::pcie;

/** 每次只推进至下一真实协议事件的可控双端。 */
class LinkRig : public LinkClockIf, public LinkPeerIf
{
  public:
    /** 使用待测数字反馈构造链路。 */
    explicit LinkRig(const LinkConfig &config = {})
        : link(*this, *this, config)
    {
        link.start();
    }
    /** 当前夹具时刻，单位 ps。 */
    uint64_t
    now() const override
    {
        return time;
    }
    /** 保留最早一次唤醒。 */
    void
    wakeAt(uint64_t when) override
    {
        due = std::min(due, when);
    }
    /** 目标收到并保留一个完整操作。 */
    bool
    receive(unsigned side, const Tlp &tlp,
            std::function<void()> release) override
    {
        if (receiver) {
            return receiver(side, tlp, std::move(release));
        }
        received.push_back(tlp);
        link.noteVisible(side, tlp.cookie);
        release();
        return true;
    }
    /** 保存可独立核验的事件。 */
    void
    observe(const LinkTrace &trace) override
    {
        traces.push_back(trace);
    }
    /** 保存取消的本地事务与提交标志。 */
    void
    cancelled(unsigned, uint64_t cookie, bool submitted) override
    {
        cancellations.emplace_back(cookie, submitted);
    }
    /** 执行下一次时钟事件，禁止无进展的循环。 */
    void
    step()
    {
        if (due == UINT64_MAX) {
            throw std::runtime_error("link fixture has no next event");
        }
        uint64_t next = due;
        due = UINT64_MAX;
        if (next < time) {
            throw std::runtime_error("link fixture time moved backwards");
        }
        time = next;
        link.tick();
    }
    /** 在有限事件数内达到指定条件。 */
    void
    until(const std::function<bool()> &done, unsigned bound = 20000)
    {
        for (unsigned i = 0; i < bound && !done(); ++i) {
            step();
        }
        if (!done()) {
            throw std::runtime_error("link fixture event bound exceeded");
        }
    }
    /** 推进指定时长，末端保留周期事件。 */
    void
    advance(uint64_t delta)
    {
        uint64_t end = time + delta;
        until([this, end] { return due > end; });
        time = end;
    }
    /** 链路达到最终协商速率并完成 VC0 初始化。 */
    void
    ready(unsigned generation = 4, unsigned width = 4)
    {
        until([this, generation, width] {
            return link.state() == LinkState::L0 && link.active(0) &&
                   link.active(1) && link.generation() == generation &&
                   link.width() == width;
        });
    }
    /** 生成强排序、自然边界内的 posted 写。 */
    static Tlp
    write(unsigned bytes = 64, uint64_t address = 0x1000)
    {
        std::vector<uint8_t> data(bytes, 0xa5);
        return Tlp::request(TlpKind::MemoryWrite, address, bytes, 0x100, 0,
                            data.data());
    }
    /** 观测某个事件的累计次数。 */
    unsigned
    count(LinkPoint point) const
    {
        return std::count_if(
            traces.begin(), traces.end(),
            [point](const LinkTrace &trace) { return trace.point == point; });
    }
    /** 当前时刻，单位 ps。 */
    uint64_t time = 0;
    /** 下一事件，无事件时为最大值。 */
    uint64_t due = UINT64_MAX;
    /** 被测双向链路。 */
    LinkModel link;
    /** 已执行的目标操作。 */
    std::vector<Tlp> received;
    /** 完整观测序列。 */
    std::vector<LinkTrace> traces;
    /** 自定义有限目标服务。 */
    std::function<bool(unsigned, const Tlp &, std::function<void()>)> receiver;
    /** 被物理复位取消的事务。 */
    std::vector<std::pair<uint64_t, bool>> cancellations;
};

/** 十二种配置均先训练至 Gen1，再通过合法 Recovery 达到目标。 */
TEST(LinkModel, TwelveModesTrain)
{
    for (unsigned generation = 1; generation <= 4; ++generation) {
        for (unsigned width : {1, 2, 4}) {
            LinkConfig config;
            config.generation = generation;
            config.width = width;
            LinkRig rig(config);
            rig.ready(generation, width);
            EXPECT_GE(rig.time, 12000000000ULL);
            EXPECT_EQ(rig.count(LinkPoint::Error), 0);
        }
    }
}

/** 接受、提交、到达、可见与 ACK 各记录一次且顺序符合语义。 */
TEST(LinkModel, SixPointsAndPipelines)
{
    LinkRig rig;
    rig.ready();
    auto tlp = LinkRig::write();
    uint64_t accepted = rig.time;
    ASSERT_TRUE(rig.link.submit(0, tlp));
    rig.until([&] {
        return rig.received.size() == 1 && rig.link.replayBytes(0) == 0;
    });
    std::vector<LinkTrace> points;
    for (const auto &trace : rig.traces) {
        if (trace.cookie == tlp.cookie) {
            points.push_back(trace);
        }
    }
    auto tick = [&](LinkPoint point) {
        auto found = std::find_if(
            points.begin(), points.end(),
            [point](const LinkTrace &trace) { return trace.point == point; });
        EXPECT_NE(found, points.end());
        return found == points.end() ? UINT64_MAX : found->tick;
    };
    EXPECT_EQ(tick(LinkPoint::Accept), accepted);
    EXPECT_GE(tick(LinkPoint::Commit) - accepted, 8000);
    EXPECT_GT(tick(LinkPoint::Receive), tick(LinkPoint::Commit));
    EXPECT_GE(tick(LinkPoint::Visible) - tick(LinkPoint::Receive), 8000);
    EXPECT_GT(tick(LinkPoint::Acknowledge), tick(LinkPoint::Receive));
    EXPECT_EQ(rig.count(LinkPoint::Commit), 1);
}

/** CRC 损坏只引发重放，目标操作执行一次。 */
TEST(LinkModel, CorruptPacketReplay)
{
    LinkRig rig;
    rig.ready();
    rig.link.inject(0, LinkFault::TlpCrc);
    auto tlp = LinkRig::write();
    ASSERT_TRUE(rig.link.submit(0, tlp));
    rig.until([&] {
        return rig.received.size() == 1 && rig.link.replayBytes(0) == 0;
    });
    EXPECT_EQ(rig.link.counters(0).replayTransmit, 1);
    EXPECT_EQ(rig.link.counters(1).crcErrors, 1);
    EXPECT_EQ(rig.link.counters(1).tlpReceive, 1);
}

/** 累计 ACK 丢失触发期限重发，已执行的数据包被重复抑制。 */
TEST(LinkModel, LostAckTimeoutAndDuplicate)
{
    LinkRig rig;
    rig.ready();
    rig.link.inject(1, LinkFault::DropAck);
    auto tlp = LinkRig::write();
    ASSERT_TRUE(rig.link.submit(0, tlp));
    rig.until([&] { return rig.link.counters(0).replayTransmit == 1; });
    rig.until([&] { return rig.link.replayBytes(0) == 0; });
    EXPECT_EQ(rig.received.size(), 1);
    EXPECT_EQ(rig.link.counters(1).duplicateReceive, 1);
}

/** 显式重复副本无需重复扣信用，也不重复执行目标写。 */
TEST(LinkModel, DuplicateFault)
{
    LinkRig rig;
    rig.ready();
    rig.link.inject(0, LinkFault::Duplicate);
    auto tlp = LinkRig::write();
    ASSERT_TRUE(rig.link.submit(0, tlp));
    rig.until([&] {
        return rig.link.replayBytes(0) == 0 &&
               rig.link.counters(1).duplicateReceive == 1;
    });
    EXPECT_EQ(rig.received.size(), 1);
}

/** 2 KiB PD 只能保留三十二个 64 B 载荷，释放后恢复传送。 */
TEST(LinkModel, ReceiveCreditBackpressure)
{
    LinkRig rig;
    rig.ready();
    std::vector<std::function<void()>> releases;
    rig.receiver = [&](unsigned, const Tlp &, std::function<void()> release) {
        releases.push_back(std::move(release));
        return true;
    };
    for (unsigned i = 0; i < 32; ++i) {
        auto tlp = LinkRig::write();
        ASSERT_TRUE(rig.link.submit(0, tlp));
    }
    rig.until([&] { return releases.size() == 32; });
    for (unsigned i = 0; i < 32; ++i) {
        auto tlp = LinkRig::write();
        ASSERT_TRUE(rig.link.submit(0, tlp));
    }
    rig.advance(1000000);
    EXPECT_EQ(rig.link.counters(0).tlpTransmit, 32);
    EXPECT_EQ(rig.link.queued(0, Traffic::Posted), 32);
    auto full = LinkRig::write();
    EXPECT_FALSE(rig.link.submit(0, full));
    for (unsigned i = 0; i < 32; ++i) {
        releases[i]();
    }
    rig.until([&] { return releases.size() == 64; });
    for (unsigned i = 32; i < 64; ++i) {
        releases[i]();
    }
    rig.link.drain();
    rig.until([&] { return rig.link.isDrained(); });
    EXPECT_EQ(rig.link.counters(0).tlpTransmit, 64);
}

/** CRC framing 错误进入 Recovery，原序号最终仍被接收一次。 */
TEST(LinkModel, FramingRecoveryPreservesReplay)
{
    LinkRig rig;
    rig.ready();
    rig.link.inject(0, LinkFault::Framing);
    auto tlp = LinkRig::write();
    ASSERT_TRUE(rig.link.submit(0, tlp));
    rig.until([&] {
        return rig.received.size() == 1 && rig.link.replayBytes(0) == 0;
    });
    EXPECT_EQ(rig.link.counters(0).tlpTransmit, 1);
    EXPECT_GE(rig.link.counters(0).replayTransmit, 1);
}

/** 每个阶段失败达到规范超时后回 Detect，不能伪造 VC0 就绪。 */
TEST(LinkModel, TrainingFailure)
{
    LinkConfig config;
    config.failTraining = LinkState::PollingConfiguration;
    LinkRig rig(config);
    rig.until([&] { return rig.count(LinkPoint::Error) == 1; });
    EXPECT_FALSE(rig.link.active(0));
    EXPECT_EQ(rig.link.state(), LinkState::DetectQuiet);
    EXPECT_GE(rig.time, 60000000000ULL);
}

/** 部分 lane 稳定检测增加十二毫秒，协商到 x2 和较低速率。 */
TEST(LinkModel, PhyCapabilityLimits)
{
    LinkConfig config;
    config.phyLaneMask = 3;
    config.phyGeneration = 2;
    LinkRig rig(config);
    rig.ready(2, 2);
    EXPECT_GE(rig.time, 24000000000ULL);
}

/** PERST 同时取消待发和已提交事务，旧接收回调无副作用。 */
TEST(LinkModel, PerstCancelsExactlyOnce)
{
    LinkRig rig;
    rig.ready();
    rig.link.inject(0, LinkFault::DropTlp);
    auto committed = LinkRig::write();
    ASSERT_TRUE(rig.link.submit(0, committed));
    rig.until([&] { return rig.link.counters(0).tlpTransmit == 1; });
    auto queued = LinkRig::write();
    ASSERT_TRUE(rig.link.submit(0, queued));
    rig.link.setPerst(true);
    EXPECT_FALSE(rig.link.active(0));
    ASSERT_EQ(rig.cancellations.size(), 2);
    EXPECT_EQ(rig.cancellations[0], std::make_pair(committed.cookie, true));
    EXPECT_EQ(rig.cancellations[1], std::make_pair(queued.cookie, false));
    rig.link.setPerst(false);
    rig.ready();
    EXPECT_EQ(rig.received.size(), 0);
}

/** ASPM 经 PM DLLP 和双向 EIOS 进入 L1，新访问经 Recovery 退出。 */
TEST(LinkModel, AspmEntryExit)
{
    LinkRig rig;
    rig.ready();
    rig.link.setAspm(0, true);
    rig.link.setAspm(1, true);
    rig.until([&] { return rig.link.state() == LinkState::L1; });
    auto tlp = LinkRig::write();
    ASSERT_TRUE(rig.link.submit(0, tlp));
    rig.until([&] { return rig.received.size() == 1; });
    EXPECT_EQ(rig.link.state(), LinkState::L0);
    EXPECT_EQ(rig.link.counters(0).tlpTransmit, 1);
}

/** D3hot 的 PCI-PM L1 独立于 ASPM 使能，D0 可恢复传输。 */
TEST(LinkModel, PciPowerEntryExit)
{
    LinkRig rig;
    rig.ready();
    rig.link.setD3hot(true);
    rig.until([&] { return rig.link.state() == LinkState::L1; });
    rig.link.setD3hot(false);
    rig.ready();
    auto tlp = LinkRig::write();
    ASSERT_TRUE(rig.link.submit(0, tlp));
    rig.until([&] { return rig.received.size() == 1; });
    EXPECT_EQ(rig.link.counters(0).tlpTransmit, 1);
}

/** drained 状态冻结维护，恢复后保持序号并继续数据传输。 */
TEST(LinkModel, DrainResume)
{
    LinkRig rig;
    rig.ready();
    auto first = LinkRig::write();
    ASSERT_TRUE(rig.link.submit(0, first));
    rig.link.drain();
    rig.until([&] { return rig.link.isDrained(); });
    auto next = LinkRig::write();
    EXPECT_FALSE(rig.link.submit(0, next));
    rig.link.resume();
    ASSERT_TRUE(rig.link.submit(0, next));
    rig.until([&] { return rig.received.size() == 2; });
    EXPECT_EQ(rig.link.counters(0).tlpTransmit, 2);
}

/** non-posted 读的完成可越过相反方向的阻塞请求并正确回传。 */
TEST(LinkModel, NonPostedCompletion)
{
    LinkRig rig;
    rig.ready();
    unsigned completions = 0;
    auto read = Tlp::request(TlpKind::MemoryRead, 0x2000, 128, 0x100, 17);
    rig.receiver = [&](unsigned side, const Tlp &tlp,
                       std::function<void()> release) {
        if (side == 1) {
            auto completion = Tlp::completion(tlp, 0x200, 0,
                                              std::vector<uint8_t>(128, 0x5a));
            EXPECT_TRUE(rig.link.submit(1, completion));
        } else {
            EXPECT_EQ(tlp.requester, 0x100);
            EXPECT_EQ(tlp.tag, 17);
            EXPECT_EQ(tlp.byteCount, 128);
            EXPECT_EQ(tlp.payload, std::vector<uint8_t>(128, 0x5a));
            ++completions;
            rig.link.noteComplete(0, read.cookie);
        }
        release();
        return true;
    };
    ASSERT_TRUE(rig.link.submit(0, read));
    rig.until([&] { return completions == 1; });
    EXPECT_EQ(rig.count(LinkPoint::Complete), 1);
}

/** 后端拒收保留 RX，显式重试后只交付一次。 */
TEST(LinkModel, BackendRetry)
{
    LinkRig rig;
    rig.ready();
    bool blocked = true;
    unsigned operations = 0;
    rig.receiver = [&](unsigned, const Tlp &, std::function<void()> release) {
        if (blocked) {
            return false;
        }
        ++operations;
        release();
        return true;
    };
    auto tlp = LinkRig::write();
    ASSERT_TRUE(rig.link.submit(0, tlp));
    rig.advance(1000000);
    EXPECT_EQ(operations, 0);
    EXPECT_EQ(rig.link.resources(1).rxHeaders[0], 1);
    blocked = false;
    rig.link.retryReceive();
    rig.until([&] { return operations == 1; });
    rig.advance(1000000);
    EXPECT_EQ(rig.link.resources(1).rxHeaders[0], 0);
    EXPECT_EQ(operations, 1);
}

/** 读写的双向顺序号与 FC 字段在回绕后仍可连续收发。 */
TEST(LinkModel, SequenceAndCreditWrap)
{
    LinkConfig config;
    config.generation = 1;
    config.width = 1;
    LinkRig rig(config);
    rig.ready(1, 1);
    for (unsigned i = 0; i < 4200; ++i) {
        auto tlp = LinkRig::write();
        ASSERT_TRUE(rig.link.submit(i & 1, tlp));
        rig.until([&] { return rig.received.size() == i + 1; });
    }
    // 每方向都跨过完整的十二位序号空间。
    for (unsigned i = 0; i < 4200; ++i) {
        auto tlp = LinkRig::write();
        ASSERT_TRUE(rig.link.submit(i & 1, tlp));
        rig.until([&] { return rig.received.size() == 4201 + i; });
    }
    EXPECT_EQ(rig.received.size(), 8400);
    EXPECT_EQ(rig.count(LinkPoint::Error), 0);
}

/** L0 和 L1 的排空快照恢复累计信用、序号及下一包相位。 */

/** 满载双向大包不能把 ACK 发送拖过表 3-7 规定的最大期限。 */
TEST(LinkModel, AckDeadlineUnderDuplexTraffic)
{
    LinkConfig config;
    config.generation = 1;
    config.width = 1;
    LinkRig rig(config);
    rig.ready(1, 1);
    ASSERT_TRUE(rig.link.configure(0, 256, false));
    ASSERT_TRUE(rig.link.configure(1, 256, false));
    for (unsigned i = 0; i < 8; ++i) {
        for (unsigned side = 0; side < 2; ++side) {
            auto tlp = LinkRig::write(256);
            ASSERT_TRUE(rig.link.submit(side, tlp));
        }
    }
    rig.link.drain();
    rig.until([&] { return rig.link.isDrained(); });
    for (unsigned side = 0; side < 2; ++side) {
        auto first_rx = std::find_if(
            rig.traces.begin(), rig.traces.end(),
            [side](const LinkTrace &trace) {
                return trace.side == side && trace.point == LinkPoint::Receive;
            });
        auto first_ack =
            std::find_if(rig.traces.begin(), rig.traces.end(),
                         [side](const LinkTrace &trace) {
                             return trace.side == side &&
                                    trace.point == LinkPoint::DllpTransmit &&
                                    trace.value == 0;
                         });
        ASSERT_NE(first_rx, rig.traces.end());
        ASSERT_NE(first_ack, rig.traces.end());
        EXPECT_LE(first_ack->frame.beginTick() - first_rx->tick, 416 * 4000);
    }
}

/** 七种数字线上故障均保持 exactly-once 目标副作用。 */
TEST(LinkModel, DigitalFaultMatrix)
{
    for (unsigned fault = 0; fault < unsigned(LinkFault::Count); ++fault) {
        LinkRig rig;
        rig.ready();
        auto kind = LinkFault(fault);
        unsigned side = kind == LinkFault::DllpCrc ||
                        kind == LinkFault::DropAck ||
                        kind == LinkFault::DropUpdate;
        rig.link.inject(side, kind);
        for (unsigned i = 0; i < 80; ++i) {
            auto request = LinkRig::write(64, 0x1000 + i * 64);
            while (!rig.link.submit(0, request)) {
                rig.step();
            }
        }
        rig.until([&] {
            return rig.received.size() == 80 && !rig.link.replayBytes(0);
        });
        std::set<uint64_t> cookies;
        for (const auto &request : rig.received) {
            EXPECT_TRUE(cookies.insert(request.cookie).second);
        }
    }
}

/** 连续四次丢 ACK 触发 replay 计数回绕和 Recovery。 */
TEST(LinkModel, ReplayRolloverRecovery)
{
    LinkRig rig;
    rig.ready();
    rig.link.inject(1, LinkFault::DropAck, 4);
    auto request = LinkRig::write();
    ASSERT_TRUE(rig.link.submit(0, request));
    rig.until([&] {
        return std::any_of(
            rig.traces.begin(), rig.traces.end(), [](const LinkTrace &trace) {
                return trace.point == LinkPoint::Error &&
                       trace.value == unsigned(LinkError::ReplayRollover);
            });
    });
    rig.until([&] {
        return rig.link.state() == LinkState::L0 && !rig.link.replayBytes(0);
    });
    EXPECT_EQ(rig.received.size(), 1);
}

/** Hot Reset 与 Link Down 各自执行数字状态和全新信用初始化。 */
TEST(LinkModel, HotResetAndLinkDown)
{
    LinkRig rig;
    rig.ready();
    auto request = LinkRig::write();
    ASSERT_TRUE(rig.link.submit(0, request));
    rig.until(
        [&] { return !rig.link.replayBytes(0) && rig.received.size() == 1; });
    rig.link.hotReset();
    EXPECT_EQ(rig.link.state(), LinkState::HotReset);
    rig.ready();
    rig.link.linkDown();
    EXPECT_EQ(rig.link.state(), LinkState::DetectQuiet);
    rig.ready();
    request.cookie = 0;
    ASSERT_TRUE(rig.link.submit(0, request));
    rig.until([&] { return rig.received.size() == 2; });
}

/** 未实现的数字故障阶段明确拒绝配置。 */
TEST(LinkModel, RejectUnsupportedFailurePhase)
{
    LinkConfig config;
    config.failTraining = LinkState::RecoverySpeed;
    EXPECT_THROW(LinkRig rig(config), std::invalid_argument);
}
