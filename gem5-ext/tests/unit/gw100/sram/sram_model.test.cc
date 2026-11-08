/**
 * @file  sram_model.test.cc
 * @brief SRAM 资源、周期、ECC 与恢复回归
 * @see spec-sram
 */
#include "src/gw100/sram/sram_model.hh"
#include <gtest/gtest.h>
#include <sstream>

namespace gem5::gw100
{
namespace
{
/** 提供保持完整组织关系的小容量测试实例。 */
SramModel::Config
config()
{
    SramModel::Config cfg;
    cfg.ecc = false;
    cfg.size = 1024;
    cfg.banks = 4;
    return cfg;
}
/** 使用实际初始化流水进入就绪。 */
void
ready(SramModel &model)
{
    model.initialize();
    for (unsigned i = 0; i < 1024 && model.state() != SramModel::State::Ready;
         ++i) {
        model.step();
    }
    ASSERT_EQ(model.state(), SramModel::State::Ready);
}
/** 通过正常提交与返回接口执行一笔有限时事务。 */
SramModel::Completion
run(SramModel &model, unsigned source, SramModel::Request request)
{
    uint64_t id;
    EXPECT_TRUE(model.submit(source, request, id));
    for (unsigned i = 0; i < 200; ++i) {
        model.step();
        if (const auto *response = model.response(source)) {
            auto result = *response;
            EXPECT_TRUE(model.acceptResponse(source));
            return result;
        }
    }
    ADD_FAILURE() << "SRAM 请求没有完成";
    return {};
}
/** 为给定范围生成顺序递增的写入数据。 */
SramModel::Request
write(uint64_t address, unsigned size = 8)
{
    SramModel::Request request;
    request.write = true;
    request.address = address;
    request.size = size;
    request.mask = size == 64 ? ~uint64_t(0) : (uint64_t(1) << size) - 1;
    for (unsigned i = 0; i < size; ++i) {
        request.data[i] = i + 1;
    }
    return request;
}

TEST(Sram, ParameterValidation)
{
    for (unsigned word : {8, 16, 32, 64}) {
        auto cfg = config();
        cfg.wordBytes = word;
        EXPECT_NO_THROW(SramModel model(cfg));
    }
    auto cfg = config();
    cfg.banks = 3;
    EXPECT_THROW(SramModel model(cfg), std::invalid_argument);
    cfg = config();
    cfg.slots = 0;
    EXPECT_THROW(SramModel model(cfg), std::invalid_argument);
}
TEST(Sram, InitializationCommits)
{
    SramModel model(config());
    model.initialize();
    model.step();
    EXPECT_EQ(model.stats().initWrites, 0);
    model.step();
    EXPECT_EQ(model.stats().initWrites, 4);
    while (model.state() != SramModel::State::Ready) {
        model.step();
    }
    EXPECT_EQ(model.now(), 33);
    EXPECT_EQ(model.stats().initWrites, 128);
}
TEST(Sram, EdgeAdmissionFollowsInitializationCommit)
{
    auto cfg = config();
    cfg.size = cfg.wordBytes;
    cfg.banks = 1;
    cfg.interval = 2;
    SramModel model(cfg);
    model.initialize();
    model.step();
    uint64_t id = 0;
    model.step([&] {
        EXPECT_EQ(model.state(), SramModel::State::Ready);
        EXPECT_TRUE(model.submit(0, write(0), id));
    });
    EXPECT_EQ(model.stats().writes, 0);
    EXPECT_EQ(model.stats().queueWait, 0);
    EXPECT_FALSE(model.drained());
    model.step();
    EXPECT_EQ(model.stats().writes, 1);
    model.step();
    ASSERT_NE(model.response(0), nullptr);
    EXPECT_EQ(model.response(0)->status, SramModel::Status::Ok);
    EXPECT_EQ(model.response(0)->data[0], 0);
    EXPECT_EQ(model.response(0)->committed, 0xff);
    EXPECT_TRUE(model.acceptResponse(0));
    EXPECT_EQ(run(model, 0, {}).data[0], 1);
}
TEST(Sram, NotReadyAndRange)
{
    SramModel model(config());
    EXPECT_EQ(run(model, 0, {}).status, SramModel::Status::NotReady);
    ready(model);
    auto request = write(1020);
    EXPECT_EQ(run(model, 0, request).status, SramModel::Status::Address);
    request = write(~uint64_t(0));
    EXPECT_EQ(run(model, 0, request).status, SramModel::Status::Address);
}
TEST(Sram, VisibilityAtCommit)
{
    auto cfg = config();
    cfg.writeCycles = 3;
    SramModel model(cfg);
    ready(model);
    uint64_t commit = 0, issue = 0, now = model.now(), id;
    model.trace = [&](const auto &event, auto, auto, auto, auto cycle) {
        if (event == "issue_write") {
            issue = cycle;
        }
        if (event == "commit") {
            commit = cycle;
        }
    };
    ASSERT_TRUE(model.submit(0, write(0), id));
    model.step();
    EXPECT_FALSE(model.response(0));
    model.step();
    model.step();
    EXPECT_EQ(commit, 0);
    model.step();
    ASSERT_TRUE(model.response(0));
    EXPECT_EQ(issue, now + 1);
    EXPECT_EQ(commit, issue + 3);
}
TEST(Sram, CrossWordAndBankRoundTrip)
{
    for (bool interleaved : {false, true}) {
        for (unsigned word : {8, 16, 32, 64}) {
            auto cfg = config();
            cfg.wordBytes = word;
            cfg.interleaved = interleaved;
            SramModel model(cfg);
            ready(model);
            for (uint64_t address :
                 {uint64_t(7), uint64_t(252), uint64_t(960)}) {
                auto request = write(address, 64);
                ASSERT_EQ(run(model, 0, request).status,
                          SramModel::Status::Ok);
                request.write = false;
                EXPECT_EQ(run(model, 0, request).data,
                          write(address, 64).data);
            }
        }
    }
}
TEST(Sram, MaskedReadModifyWrite)
{
    SramModel model(config());
    ready(model);
    run(model, 0, write(0));
    auto partial = write(1, 2);
    partial.data[0] = 0xa5;
    partial.data[1] = 0x5a;
    run(model, 1, partial);
    auto result = run(model, 0, {});
    EXPECT_EQ(result.data[0], 1);
    EXPECT_EQ(result.data[1], 0xa5);
    EXPECT_EQ(result.data[2], 0x5a);
    EXPECT_EQ(result.data[3], 4);
    EXPECT_EQ(model.stats().rmw, 1);
}

TEST(Sram, SameWordReadWriteCollisionIsSerialized)
{
    auto cfg = config();
    cfg.splitPorts = true;
    SramModel model(cfg);
    ready(model);
    uint64_t id;
    EXPECT_TRUE(model.submit(0, write(0), id));
    EXPECT_TRUE(model.submit(1, {}, id));
    std::vector<uint64_t> issued;
    model.trace = [&](const auto &event, auto, auto, auto, auto cycle) {
        if (event == "issue_read" || event == "issue_write") {
            issued.push_back(cycle);
        }
    };
    for (unsigned i = 0; i < 5; ++i) {
        model.step();
    }
    ASSERT_EQ(issued.size(), 2);
    EXPECT_NE(issued[0], issued[1]);
}

TEST(Sram, NotReadyResponseDuringInitialization)
{
    SramModel model(config());
    model.initialize();
    uint64_t id;
    ASSERT_TRUE(model.submit(0, {}, id));
    model.step();
    ASSERT_TRUE(model.response(0));
    EXPECT_EQ(model.response(0)->status, SramModel::Status::NotReady);
    EXPECT_EQ(model.state(), SramModel::State::Initializing);
}

TEST(Sram, IndependentInstancesAndInvalidMask)
{
    SramModel first(config()), second(config());
    ready(first);
    ready(second);
    run(first, 0, write(0));
    EXPECT_EQ(run(second, 0, {}).data[0], 0);
    auto request = write(0, 1);
    request.mask = 2;
    EXPECT_EQ(run(first, 0, request).status, SramModel::Status::Address);
    EXPECT_EQ(run(first, 0, {}).data[0], 1);
}

TEST(Sram, OverlappingRmwKeepsBothWrites)
{
    auto cfg = config();
    cfg.wordBytes = 16;
    cfg.splitPorts = true;
    cfg.readCycles = cfg.writeCycles = 3;
    SramModel model(cfg);
    ready(model);
    uint64_t id;
    auto first = write(0, 1), second = write(1, 1);
    first.data[0] = 0x5a;
    second.data[0] = 0xa5;
    ASSERT_TRUE(model.submit(0, first, id));
    ASSERT_TRUE(model.submit(1, second, id));
    for (unsigned i = 0; i < 30; ++i) {
        model.step();
    }
    ASSERT_TRUE(model.acceptResponse(0));
    ASSERT_TRUE(model.acceptResponse(1));
    auto result = run(model, 2, {});
    EXPECT_EQ(result.data[0], 0x5a);
    EXPECT_EQ(result.data[1], 0xa5);
    for (unsigned i = 2; i < 8; ++i) {
        EXPECT_EQ(result.data[i], 0);
    }
}

TEST(Sram, UncontendedWaitCounters)
{
    SramModel model(config());
    ready(model);
    run(model, 0, write(0));
    EXPECT_EQ(model.stats().queueWait, 0);
    EXPECT_EQ(model.stats().bankWait, 0);
    EXPECT_EQ(model.stats().dependencyWait, 0);
}

TEST(Sram, ForeignWordLockCountsAsArbitration)
{
    SramModel model(config());
    ready(model);
    uint64_t id;
    ASSERT_TRUE(model.submit(0, write(0, 1), id));
    ASSERT_TRUE(model.submit(1, write(1, 1), id));
    for (unsigned i = 0; i < 10; ++i) {
        model.step();
    }
    EXPECT_GT(model.stats().bankWait, 0);
    EXPECT_EQ(model.stats().dependencyWait, 0);
}

TEST(Sram, CancelledIngressKeepsOldEpoch)
{
    SramModel model(config());
    ready(model);
    uint64_t old = model.epoch(), id;
    model.reset(true);
    ASSERT_TRUE(model.submit(0, write(0), id, old));
    model.step();
    ASSERT_TRUE(model.response(0));
    EXPECT_EQ(model.response(0)->status, SramModel::Status::Reset);
    EXPECT_EQ(model.response(0)->epoch, old);
    EXPECT_EQ(model.response(0)->committed, 0);
}

TEST(Sram, FiniteSlotsIncludeBlockedResponses)
{
    auto cfg = config();
    cfg.slots = 2;
    SramModel model(cfg);
    ready(model);
    uint64_t id;
    ASSERT_TRUE(model.submit(0, write(0), id));
    EXPECT_FALSE(model.submit(0, write(8), id));
    model.step();
    ASSERT_TRUE(model.submit(0, write(8), id));
    for (unsigned i = 0; i < 8; ++i) {
        model.step();
    }
    EXPECT_FALSE(model.submit(0, write(16), id));
    EXPECT_FALSE(model.drained());
    ASSERT_TRUE(model.acceptResponse(0));
    ASSERT_TRUE(model.submit(0, write(16), id));
}
TEST(Sram, SplitPortsAndCollision)
{
    for (bool split : {false, true}) {
        auto cfg = config();
        cfg.splitPorts = split;
        SramModel model(cfg);
        ready(model);
        uint64_t id, issuedRead = 0, issuedWrite = 0;
        model.trace = [&](const auto &event, auto, auto, auto, auto cycle) {
            if (event == "issue_read") {
                issuedRead = cycle;
            }
            if (event == "issue_write") {
                issuedWrite = cycle;
            }
        };
        EXPECT_TRUE(model.submit(0, {}, id));
        EXPECT_TRUE(model.submit(1, write(8), id));
        for (unsigned i = 0; i < 5; ++i) {
            model.step();
        }
        EXPECT_EQ(issuedWrite - issuedRead, split ? 0 : 1);
    }
}
TEST(Sram, ResetCancelsPendingWrite)
{
    auto cfg = config();
    cfg.writeCycles = 3;
    SramModel model(cfg);
    ready(model);
    uint64_t id;
    EXPECT_TRUE(model.submit(0, write(0), id));
    model.step();
    model.reset(true);
    ASSERT_TRUE(model.response(0));
    EXPECT_EQ(model.response(0)->status, SramModel::Status::Reset);
    model.acceptResponse(0);
    for (unsigned i = 0; i < 5; ++i) {
        model.step();
    }
    EXPECT_EQ(run(model, 0, {}).data[0], 0);
}
TEST(Sram, FairnessIncludesPartialWrites)
{
    auto cfg = config();
    cfg.size = 256;
    SramModel model(cfg);
    ready(model);
    uint64_t id;
    for (unsigned cycle = 0; cycle < 300; ++cycle) {
        for (unsigned s = 0; s < cfg.sources; ++s) {
            model.submit(s, write(s * 8, 4), id);
            if (model.response(s)) {
                model.acceptResponse(s);
            }
        }
        model.step();
    }
    EXPECT_LE(model.stats().maxBankWait, 16);
    EXPECT_GT(model.stats().maxBankWait, 0);
}
} // namespace
} // namespace gem5::gw100
