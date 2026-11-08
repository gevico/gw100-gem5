/**
 * @file  link_transport.test.cc
 * @brief PCIe 完成重组、真实 tag、有限目标与生命周期的事务测试
 *
 * @see spec-pcie, spec-host-dma
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <vector>

#include "src/gw100/pcie/link/link_transport.hh"

using namespace gem5;
using namespace gem5::gw100;
using namespace gem5::gw100::pcie;

/** 拥有有限双端存储和真实服务等待的事务夹具。 */
class TransportRig : public LinkClockIf
{
  public:
    /** 连接目标与共享链路。 */
    TransportRig(unsigned crs_limit = 2)
    {
        LinkServices services;
        services.execute = [this](unsigned side, const Tlp &request,
                                  TargetDone done) {
            executed.push_back(request);
            if (request.isConfig() && crsRemaining) {
                --crsRemaining;
                backend.emplace(time + delay,
                                [done] { done(CompletionStatus::Crs, {}); });
                return true;
            }
            if (drop) {
                return true;
            }
            backend.emplace(time + delay, [this, side, request, done] {
                unsigned bytes = request.requestBytes();
                unsigned address = request.address + request.firstOffset();
                std::vector<uint8_t> data;
                if (request.kind == TlpKind::MemoryWrite ||
                    request.kind == TlpKind::ConfigWrite) {
                    std::copy_n(request.payload.begin() +
                                    request.firstOffset(),
                                bytes, memory[side].begin() + address);
                } else {
                    data.assign(memory[side].begin() + address,
                                memory[side].begin() + address + bytes);
                }
                done(CompletionStatus::Success, data);
            });
            return true;
        };
        services.ready = [this] { wakeAt(time); };
        services.intx = [this](bool level) { intx.push_back(level); };
        services.error = [this](unsigned, LinkError error,
                                const std::vector<uint8_t> &) {
            errors.push_back(error);
        };
        services.observe = [this](const LinkTrace &trace) {
            traces.push_back(trace);
        };
        transport = std::make_unique<LinkTransport>(*this, LinkConfig{}, 8,
                                                    1000000000, 2000000000, 64,
                                                    256, services, crs_limit);
        for (auto &store : memory) {
            store.resize(1 << 20);
        }
        transport->model().start();
        until([this] {
            return transport->model().active(0) &&
                   transport->model().active(1) &&
                   transport->model().generation() == 4 &&
                   transport->model().state() == LinkState::L0;
        });
        EXPECT_TRUE(transport->configure(256, false));
        transport->setActive(true);
    }
    /** 当前时刻，单位 ps。 */
    uint64_t
    now() const override
    {
        return time;
    }
    /** 保留最早协议事件。 */
    void
    wakeAt(uint64_t when) override
    {
        due = std::min(due, when);
    }
    /** 有限事件数内达到测试条件。 */
    void
    until(const std::function<bool()> &done)
    {
        unsigned steps = 0;
        while (!done()) {
            if (++steps == 100000) {
                throw std::runtime_error("transport fixture event bound");
            }
            uint64_t next = due;
            if (!backend.empty()) {
                next = std::min(next, backend.begin()->first);
            }
            if (next == UINT64_MAX || next < time) {
                throw std::runtime_error("transport fixture stalled");
            }
            time = next;
            due = UINT64_MAX;
            while (!backend.empty() && backend.begin()->first <= time) {
                auto action = std::move(backend.begin()->second);
                backend.erase(backend.begin());
                action();
            }
            transport->tick();
        }
    }
    /** 当前时刻，单位 ps。 */
    uint64_t time = 0;
    /** 最早协议期限，单位 ps。 */
    uint64_t due = UINT64_MAX;
    /** 目标实际服务延迟，单位 ps。 */
    uint64_t delay = 250000;
    /** 目标保留请求但不返回完成，用于超时和隔离测试。 */
    bool drop = false;
    /** 配置目标暂时返回 CRS 的次数。 */
    unsigned crsRemaining = 0;
    /** 被测适配器。 */
    std::unique_ptr<LinkTransport> transport;
    /** 双端目标数据。 */
    std::array<std::vector<uint8_t>, 2> memory;
    /** 有界后端已经接受的线上请求。 */
    std::vector<Tlp> executed;
    /** 独立目标定时事件。 */
    std::multimap<uint64_t, std::function<void()>> backend;
    /** 实际报告的协议错误。 */
    std::vector<LinkError> errors;
    /** RC 观察到的 INTx 电平。 */
    std::vector<bool> intx;
    /** 原始线上与本地结算事件。 */
    std::vector<LinkTrace> traces;
};

/** RC 的 NP 达到分类上限后保留 posted 进展所需的后端槽。 */
TEST(LinkTransport, RcPostedReservation)
{
    TransportRig rig;
    rig.drop = true;
    for (unsigned tag = 0; tag < 48; ++tag) {
        ASSERT_TRUE(rig.transport->readTagged(
            0x1000, 64, tag, {}, [](HostStatus, const uint8_t *) {}));
        rig.until([&] { return rig.executed.size() == tag + 1; });
    }
    ASSERT_TRUE(rig.transport->readTagged(0x1000, 64, 48, {},
                                          [](HostStatus, const uint8_t *) {}));
    rig.until([&] {
        return std::count_if(rig.traces.begin(), rig.traces.end(),
                             [](const LinkTrace &trace) {
                                 return trace.point == LinkPoint::Receive &&
                                        trace.side == 0;
                             }) == 49;
    });
    EXPECT_EQ(rig.executed.size(), 48);
    rig.drop = false;
    uint8_t data[64] = {0xa5};
    ASSERT_TRUE(rig.transport->canWrite());
    rig.transport->write(0x2000, sizeof(data), data);
    rig.until([&] { return rig.memory[0][0x2000] == 0xa5; });
    EXPECT_EQ(rig.executed.size(), 49);
    EXPECT_EQ(rig.executed.back().kind, TlpKind::MemoryWrite);
    EXPECT_EQ(rig.transport->model().resources(0).rxHeaders[1], 1);
    EXPECT_TRUE(rig.errors.empty());
}

/** 普通读在先前 posted 写实际可见后才读取目标数据。 */
TEST(LinkTransport, ReadWaitsForPostedVisibility)
{
    TransportRig rig;
    rig.delay = 2500000;
    uint8_t data[64] = {0xa5};
    rig.transport->write(0x2000, sizeof(data), data);
    rig.until([&] { return rig.executed.size() == 1; });
    rig.delay = 0;
    bool done = false;
    ASSERT_TRUE(rig.transport->readTagged(
        0x2000, sizeof(data), 0, {},
        [&](HostStatus status, const uint8_t *result) {
            EXPECT_EQ(status, HostStatus::Ok);
            EXPECT_EQ(result[0], 0xa5);
            done = true;
        }));
    rig.until([&] { return done; });
    EXPECT_EQ(rig.memory[0][0x2000], 0xa5);
    EXPECT_TRUE(rig.errors.empty());
}

/** 阻塞读的依赖固定在首次到达时，后续写继续推进且不会饿死读。 */
TEST(LinkTransport, ReadDoesNotWaitForYoungerPosted)
{
    TransportRig rig;
    rig.delay = 2500000;
    uint8_t data[64] = {0xa5};
    rig.transport->write(0x2000, sizeof(data), data);
    rig.until([&] { return rig.executed.size() == 1; });
    bool done = false;
    ASSERT_TRUE(rig.transport->readTagged(
        0x2000, sizeof(data), 0, {},
        [&](HostStatus status, const uint8_t *result) {
            EXPECT_EQ(status, HostStatus::Ok);
            EXPECT_EQ(result[0], 0xa5);
            done = true;
        }));
    rig.until([&] {
        return std::count_if(rig.traces.begin(), rig.traces.end(),
                             [](const LinkTrace &trace) {
                                 return trace.point == LinkPoint::Receive &&
                                        trace.side == 0;
                             }) == 2;
    });
    rig.delay = 10000000;
    rig.transport->write(0x3000, sizeof(data), data);
    rig.until([&] { return rig.executed.size() == 2; });
    rig.delay = 0;
    rig.until([&] { return done; });
    EXPECT_EQ(rig.memory[0][0x3000], 0);
    EXPECT_TRUE(rig.errors.empty());
}

/** CRS 保留上游请求，在有界等待后以新的线上事务重发。 */
TEST(LinkTransport, ConfigurationCrsRetry)
{
    TransportRig rig;
    rig.crsRemaining = 2;
    rig.memory[1][0] = 0xa5;
    auto request = Tlp::request(TlpKind::ConfigRead, 0, 4, 0, 0);
    request.destination = 8;
    unsigned done = 0;
    ASSERT_TRUE(rig.transport->submitPio(
        request, [&](HostStatus status, const uint8_t *data) {
            EXPECT_EQ(status, HostStatus::Ok);
            EXPECT_EQ(data[0], 0xa5);
            ++done;
        }));
    rig.until([&] { return done == 1; });
    EXPECT_EQ(rig.executed.size(), 3);
    EXPECT_NE(rig.executed[0].cookie, rig.executed[1].cookie);
    EXPECT_NE(rig.executed[1].cookie, rig.executed[2].cookie);
    EXPECT_TRUE(rig.errors.empty());
}

/** 超过参考 RC 的 CRS 重试预算时恰好一次失败。 */
TEST(LinkTransport, ConfigurationCrsBound)
{
    TransportRig rig;
    rig.crsRemaining = 3;
    auto request = Tlp::request(TlpKind::ConfigRead, 0, 4, 0, 0);
    unsigned done = 0;
    ASSERT_TRUE(rig.transport->submitPio(
        request, [&](HostStatus status, const uint8_t *) {
            EXPECT_EQ(status, HostStatus::Ca);
            ++done;
        }));
    rig.until([&] { return done == 1; });
    EXPECT_EQ(rig.executed.size(), 3);
    EXPECT_EQ(rig.transport->outstanding(0), 0);
}

/** 完成状态、poison、BCM 和额外数据分别终止当前请求。 */
TEST(LinkTransport, CompletionFaultMatrix)
{
    for (unsigned fault = 0; fault < 7; ++fault) {
        TransportRig rig;
        rig.drop = true;
        unsigned done = 0;
        ASSERT_TRUE(rig.transport->readTagged(
            0x1000, 64, 5, {}, [&](HostStatus status, const uint8_t *) {
                EXPECT_NE(status, HostStatus::Ok);
                ++done;
            }));
        rig.until([&] { return !rig.executed.empty(); });
        auto completion = Tlp::completion(rig.executed[0], 0, 0,
                                          std::vector<uint8_t>(64, 0));
        if (fault < 2) {
            completion.status =
                fault ? CompletionStatus::Ca : CompletionStatus::Ur;
            completion.payload.clear();
        } else if (fault == 2) {
            completion.poisoned = true;
        } else if (fault == 3) {
            completion.bcm = true;
        } else if (fault == 4) {
            completion.lowerAddress = 4;
        } else if (fault == 5) {
            completion.tc = 0;
            completion.byteCount = 128;
        } else {
            completion.payload.resize(68);
            completion.length = 17;
        }
        ASSERT_TRUE(rig.transport->model().submit(0, completion));
        rig.until([&] { return done == 1; });
        EXPECT_EQ(rig.transport->outstanding(1), 0);
        EXPECT_FALSE(rig.errors.empty());
    }
}

/** 不支持的 I/O 请求返回 UR，poisoned 配置写保持目标内容。 */
TEST(LinkTransport, UnsupportedAndPoisonedRequests)
{
    for (bool poisoned : {false, true}) {
        TransportRig rig;
        uint8_t value[] = {1, 2, 3, 4};
        auto request = Tlp::request(TlpKind::ConfigWrite, 0, 4, 0, 0, value);
        if (poisoned) {
            request.poisoned = true;
        } else {
            request.kind = TlpKind::Unsupported;
            request.unsupportedFmt = 2;
            request.unsupportedType = 2;
        }
        unsigned done = 0;
        ASSERT_TRUE(rig.transport->submitPio(
            request, [&](HostStatus status, const uint8_t *) {
                EXPECT_EQ(status, HostStatus::Ur);
                ++done;
            }));
        rig.until([&] { return done == 1; });
        EXPECT_TRUE(rig.executed.empty());
        EXPECT_EQ(rig.memory[1][0], 0);
        EXPECT_EQ(rig.errors.front(), poisoned
                                          ? LinkError::Poisoned
                                          : LinkError::UnsupportedRequest);
    }
}

/** 显式请求的 Attr[1:0] 在 MRd/MWr 线上保持。 */
TEST(LinkTransport, WireAttributes)
{
    TransportRig rig;
    bool done = false;
    ASSERT_TRUE(rig.transport->readTagged(
        0x1000, 64, 2, {},
        [&](HostStatus status, const uint8_t *) {
            EXPECT_EQ(status, HostStatus::Ok);
            done = true;
        },
        3));
    rig.until([&] { return done; });
    EXPECT_EQ(rig.executed.back().attributes, 3);
    uint8_t data[64] = {};
    rig.transport->write(0x1000, 64, data, 3);
    rig.until([&] { return rig.executed.size() == 2; });
    EXPECT_EQ(rig.executed.back().attributes, 3);
}

/** 事务快照保存 L0/L1、INTx、tag 和已完成的数据链路状态。 */
TEST(LinkTransport, DrainedSnapshotRoundTrip)
{
    for (bool low_power : {false, true}) {
        TransportRig rig;
        rig.transport->setIntx(true);
        rig.until([&] { return !rig.intx.empty() && rig.intx.back(); });
        if (low_power) {
            rig.transport->model().setD3hot(true);
            rig.until([&] {
                return rig.transport->model().state() == LinkState::L1;
            });
        }
        rig.transport->model().drain();
        rig.until([&] { return rig.transport->model().isDrained(); });
        auto saved = rig.transport->snapshot();
        rig.transport->restore(saved);
        EXPECT_TRUE(rig.transport->model().isDrained());
        EXPECT_EQ(rig.transport->snapshot().link.state, saved.link.state);
        EXPECT_TRUE(rig.intx.back());
        rig.transport->model().resume();
        rig.transport->model().setD3hot(false);
        bool done = false;
        ASSERT_TRUE(rig.transport->readTagged(
            0x1000, 64, 9, {}, [&](HostStatus status, const uint8_t *) {
                EXPECT_EQ(status, HostStatus::Ok);
                done = true;
            }));
        rig.until([&] { return done; });
    }
}

/** 八位线上 tag 和 Requester ID 原样穿过 MRd 与分段 completion。 */
TEST(LinkTransport, WholeReadAndWireTag)
{
    TransportRig rig;
    for (unsigned i = 0; i < 512; ++i) {
        rig.memory[0][0x1000 + i] = i ^ 0xa5;
    }
    bool committed = false;
    bool completed = false;
    std::vector<uint8_t> result;
    ASSERT_TRUE(rig.transport->readTagged(
        0x1000, 512, 219, [&] { committed = true; },
        [&](HostStatus status, const uint8_t *data) {
            EXPECT_TRUE(committed);
            EXPECT_EQ(status, HostStatus::Ok);
            result.assign(data, data + 512);
            completed = true;
        }));
    rig.until([&] { return completed; });
    ASSERT_EQ(rig.executed.size(), 1);
    EXPECT_EQ(rig.executed[0].tag, 219);
    EXPECT_EQ(rig.executed[0].requester, 8);
    EXPECT_EQ(rig.executed[0].length, 128);
    EXPECT_EQ(result, std::vector<uint8_t>(rig.memory[0].begin() + 0x1000,
                                           rig.memory[0].begin() + 0x1200));
    EXPECT_TRUE(rig.errors.empty());
}

/** 配置的单字节 BE 只影响目标字节，completion 使用固定配置字段。 */
TEST(LinkTransport, ConfigurationReadWrite)
{
    TransportRig rig;
    uint8_t value = 0x5a;
    bool written = false;
    auto write = Tlp::request(TlpKind::ConfigWrite, 0x43, 1, 0, 0, &value);
    ASSERT_TRUE(rig.transport->submitPio(
        write, [&](HostStatus status, const uint8_t *) {
            EXPECT_EQ(status, HostStatus::Ok);
            written = true;
        }));
    rig.until([&] { return written; });
    EXPECT_EQ(rig.memory[1][0x42], 0);
    EXPECT_EQ(rig.memory[1][0x43], value);
    bool read = false;
    auto request = Tlp::request(TlpKind::ConfigRead, 0x43, 1, 0, 0);
    ASSERT_TRUE(rig.transport->submitPio(
        request, [&](HostStatus status, const uint8_t *data) {
            EXPECT_EQ(status, HostStatus::Ok);
            EXPECT_EQ(data[0], value);
            read = true;
        }));
    rig.until([&] { return read; });
    EXPECT_TRUE(rig.errors.empty());
}

/** DLL 提交前撤销不执行目标，也不保留读缓冲或 tag 匹配元数据。 */
TEST(LinkTransport, CancelBeforeCommit)
{
    TransportRig rig;
    bool committed = false;
    ASSERT_TRUE(rig.transport->readTagged(
        0x1000, 64, 7, [&] { committed = true; },
        [](HostStatus, const uint8_t *) {
            ADD_FAILURE() << "cancelled request completed";
        }));
    rig.transport->cancelRead(7);
    EXPECT_FALSE(committed);
    EXPECT_EQ(rig.transport->outstanding(1), 0);
    rig.transport->model().drain();
    rig.until([&] { return rig.transport->model().isDrained(); });
    EXPECT_TRUE(rig.executed.empty());
}

/** 已提交 tag 隔离后，迟到合法 completion 只能结算旧请求。 */
TEST(LinkTransport, IsolatedLateCompletion)
{
    TransportRig rig;
    rig.drop = true;
    bool committed = false;
    unsigned done = 0;
    ASSERT_TRUE(rig.transport->readTagged(
        0x1000, 64, 17, [&] { committed = true; },
        [&](HostStatus, const uint8_t *) { ++done; }));
    rig.until([&] { return committed && !rig.executed.empty(); });
    rig.transport->isolateRead(17);
    EXPECT_FALSE(rig.transport->readTagged(0x2000, 64, 17, {}, {}));
    auto completion =
        Tlp::completion(rig.executed[0], 0, 0, std::vector<uint8_t>(64, 0x5a));
    ASSERT_TRUE(rig.transport->model().submit(0, completion));
    rig.until([&] { return rig.transport->outstanding(1) == 0; });
    EXPECT_EQ(done, 0);
}

/** RC 本地超时后保留 tag 隔离，完整迟到完成不再回调上游。 */
TEST(LinkTransport, RcTimeoutIsolatesWireTag)
{
    TransportRig rig;
    rig.drop = true;
    auto original = Tlp::request(TlpKind::MemoryRead, 0x1000, 64, 0, 0);
    unsigned done = 0;
    ASSERT_TRUE(rig.transport->submitPio(
        original, [&](HostStatus status, const uint8_t *) {
            EXPECT_EQ(status, HostStatus::Ca);
            ++done;
        }));
    rig.until([&] { return done == 1; });
    EXPECT_EQ(rig.transport->outstanding(0), 1);
    auto request = rig.executed.front();
    auto late = Tlp::completion(request, 8, 0, std::vector<uint8_t>(64, 0));
    ASSERT_TRUE(rig.transport->model().submit(1, late));
    rig.until([&] { return rig.transport->outstanding(0) == 0; });
    EXPECT_EQ(done, 1);
    EXPECT_EQ(std::count_if(rig.traces.begin(), rig.traces.end(),
                            [](const LinkTrace &trace) {
                                return trace.point == LinkPoint::Retired;
                            }),
              1);
}

/** 隔离期后复用相同协议字段，硬件无法辨别足够迟到的旧完成。 */
TEST(LinkTransport, WireIdenticalTagReuseBoundary)
{
    TransportRig rig;
    rig.drop = true;
    bool committed = false;
    ASSERT_TRUE(rig.transport->readTagged(0x1000, 64, 9,
                                          [&] { committed = true; }, {}));
    rig.until([&] { return committed && rig.executed.size() == 1; });
    auto original = rig.executed.front();
    rig.transport->isolateRead(9);
    rig.until([&] { return rig.transport->outstanding(1) == 0; });
    unsigned done = 0;
    ASSERT_TRUE(rig.transport->readTagged(
        0x1000, 64, 9, {}, [&](HostStatus status, const uint8_t *data) {
            EXPECT_EQ(status, HostStatus::Ok);
            EXPECT_EQ(data[0], 0x5a);
            ++done;
        }));
    rig.until([&] { return rig.executed.size() == 2; });
    auto late =
        Tlp::completion(original, 0, 0, std::vector<uint8_t>(64, 0x5a));
    ASSERT_TRUE(rig.transport->model().submit(0, late));
    rig.until([&] { return done == 1; });
    EXPECT_TRUE(rig.errors.empty());
}

/** Byte Count 错误结束当前请求并报告 malformed，重复片段无新副作用。 */
TEST(LinkTransport, MalformedAndUnexpectedCompletion)
{
    TransportRig rig;
    rig.drop = true;
    bool completed = false;
    ASSERT_TRUE(rig.transport->readTagged(
        0x1000, 64, 4, {}, [&](HostStatus status, const uint8_t *) {
            EXPECT_EQ(status, HostStatus::Ca);
            completed = true;
        }));
    rig.until([&] { return !rig.executed.empty(); });
    auto completion =
        Tlp::completion(rig.executed[0], 0, 0, std::vector<uint8_t>(64, 0));
    completion.byteCount = 63;
    ASSERT_TRUE(rig.transport->model().submit(0, completion));
    rig.until([&] { return completed; });
    ASSERT_EQ(rig.errors.size(), 1);
    EXPECT_EQ(rig.errors[0], LinkError::Malformed);
    completion.cookie = 0;
    ASSERT_TRUE(rig.transport->model().submit(0, completion));
    rig.until([&] { return rig.errors.size() == 2; });
    EXPECT_EQ(rig.errors[1], LinkError::UnexpectedCompletion);
}

/** DMA 数据、完成记录与 INTx 共用同一个 posted 排序通路。 */
TEST(LinkTransport, PostedVisibilityAndIntx)
{
    TransportRig rig;
    std::vector<uint8_t> data(256, 0x5a);
    ASSERT_TRUE(rig.transport->canWrite());
    rig.transport->write(0x1000, data.size(), data.data());
    rig.transport->setIntx(true);
    rig.until([&] { return rig.intx.size() >= 2 && rig.intx.back(); });
    EXPECT_EQ(rig.memory[0][0x1000], 0x5a);
    rig.until([&] { return rig.memory[0][0x1000] == 0x5a; });
    EXPECT_EQ(rig.executed.size(), 1);
    rig.transport->setIntx(false);
    rig.until([&] { return !rig.intx.back(); });
    EXPECT_TRUE(rig.errors.empty());
}

/** 完成上限小于 RCB 时明确拒绝通用大读配置。 */
TEST(LinkTransport, RejectIncompatibleCompletionRcb)
{
    TransportRig rig;
    LinkServices services;
    services.execute = [](unsigned, const Tlp &, TargetDone) { return true; };
    EXPECT_THROW(
        LinkTransport(rig, LinkConfig{}, 8, 1000, 2000, 128, 64, services),
        std::invalid_argument);
}

/** 零长度读完成 flush，零长度写无效果，两者均不触发目标副作用。 */
TEST(LinkTransport, ZeroLengthReadAndWriteNoSideEffects)
{
    TransportRig rig;
    auto read = Tlp::request(TlpKind::MemoryRead, 0x1000, 4, 0, 0);
    read.firstBe = 0;
    bool completed = false;
    ASSERT_TRUE(rig.transport->submitPio(
        read, [&](HostStatus status, const uint8_t *) {
            EXPECT_EQ(status, HostStatus::Ok);
            completed = true;
        }));
    rig.until([&] { return completed; });
    uint32_t data = 0x55555555;
    auto write = Tlp::request(TlpKind::MemoryWrite, 0x1000, 4, 0, 0,
                              reinterpret_cast<uint8_t *>(&data));
    write.firstBe = 0;
    ASSERT_TRUE(rig.transport->submitPio(write, {}));
    rig.transport->model().drain();
    rig.until([&] { return rig.transport->model().isDrained(); });
    EXPECT_EQ(rig.memory[1][0x1000], 0);
    EXPECT_TRUE(rig.executed.empty());
}
