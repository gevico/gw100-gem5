/**
 * @file  link_serializer.test.cc
 * @brief 独立手算核对十二种链路的编码时间和跨包相位
 *
 * 期望由位数和链路速率直接计算，逐项检查编码块、SKP、IDL 与训练。
 *
 * @see spec-pcie
 */

#include <gtest/gtest.h>

#include "src/gw100/pcie/link/serializer.hh"

using namespace gem5::gw100::pcie;

/** 十二种速率宽度下，一包的最后符号边沿符合逐 bit 手算。 */
TEST(LinkSerializer, TwelveModes)
{
    const uint64_t half_bit[] = {800, 400, 250, 125};
    for (unsigned generation = 1; generation <= 4; ++generation) {
        for (unsigned width : {1, 2, 4}) {
            Serializer serializer(generation, width);
            auto stream = serializer.beginStream(0);
            for (unsigned bytes : {20, 24, 64, 148, 152, 276, 280}) {
                uint64_t before = serializer.state().dataSymbols;
                auto frame = serializer.packet(bytes, 0);
                uint64_t symbols = before + bytes / width;
                uint64_t bits = symbols * 10;
                if (generation >= 3) {
                    bits = (symbols / 16) * 130;
                    if (symbols % 16) {
                        bits += 2 + (symbols % 16) * 8;
                    }
                }
                EXPECT_EQ(frame.endHalf,
                          stream.endHalf + bits * half_bit[generation - 1]);
                EXPECT_GE(frame.endHalf, frame.beginHalf);
            }
        }
    }
}

/** 多包持续传输不逐包摊销或重置 128b/130b 块头。 */
TEST(LinkSerializer, ContinuousPhase)
{
    Serializer serializer(4, 4);
    serializer.beginStream(0);
    uint64_t headers = 0;
    for (unsigned i = 0; i < 10000; ++i) {
        auto frame = serializer.packet(20, 0);
        headers += frame.encodingHalf;
    }
    uint64_t symbols = 10000 * 5;
    EXPECT_EQ(headers, ((symbols + 15) / 16) * 250);
    EXPECT_EQ(serializer.state().endHalf, 32500 + (symbols / 16) * 16250);
}

/** 空闲继续推进数据块，在完整 lane 0 符号处开始下一包。 */
TEST(LinkSerializer, IdleKeepsBlockPhase)
{
    Serializer serializer(4, 4);
    serializer.beginStream(0);
    serializer.packet(20, 0);
    auto frame = serializer.packet(20, 100000);
    EXPECT_GE(frame.beginHalf, 200000);
    EXPECT_LT(frame.beginHalf - 200000, 1250);
    EXPECT_NE(serializer.state().dataSymbols % 16, 5);
}

/** SKP 先补齐 EDS 所在块，随后一个 Ordered Set，再继续数据流。 */
TEST(LinkSerializer, SkipBlockBoundary)
{
    Serializer serializer(4, 4);
    auto stream = serializer.beginStream(0);
    auto packet = serializer.packet(20, 0);
    auto skip = serializer.skip(0);
    EXPECT_EQ(skip.endHalf, stream.endHalf + 2 * 130 * 125);
    EXPECT_EQ(skip.orderedHalf, 130 * 125);
    EXPECT_GT(skip.paddingHalf, 0);
    EXPECT_EQ(serializer.state().dataSymbols, 16);
    auto next = serializer.packet(8, 0);
    EXPECT_EQ(next.beginHalf, skip.endHalf + 250);
    EXPECT_LT(packet.endHalf, skip.endHalf);
}

/** x1 接近块尾时 EDS 需四个符号，空间不足则使用下一完整块。 */
TEST(LinkSerializer, SkipNeedsWholeEds)
{
    Serializer serializer(3, 1);
    serializer.beginStream(0);
    serializer.packet(12, 0);
    auto first = serializer.skip(0);
    EXPECT_EQ(serializer.state().dataSymbols, 16);
    serializer.packet(28, 0);
    auto second = serializer.skip(0);
    EXPECT_EQ(serializer.state().dataSymbols, 48);
    EXPECT_GT(second.endHalf, first.endHalf);
}

/** 1024 个 Gen1 TS1 的规定时长包含十二个周期 SKP。 */
TEST(LinkSerializer, TrainingMinimumAndSkip)
{
    Serializer serializer(1, 4);
    auto training = serializer.training(1024, 12000000000);
    EXPECT_EQ(training.skpCount, 12);
    EXPECT_EQ(training.endTick() - training.beginTick(),
              1024 * 16 * 4000 + 12 * 4 * 4000);
}

/** 高速训练的每三十二个 TS 后还需发送 EIEOS。 */
TEST(LinkSerializer, PeriodicElectricalIdleExit)
{
    Serializer serializer(4, 4);
    auto initial = serializer.beginRecovery(0);
    EXPECT_EQ(initial.endHalf, 130 * 125);
    auto training = serializer.training(33, 0);
    EXPECT_EQ(training.endHalf - initial.endHalf, 34 * 130 * 125);
    serializer.resetTrainingCount();
    auto next = serializer.training(32, 0);
    EXPECT_EQ(next.endHalf - training.endHalf, 33 * 130 * 125);
}

/** 电气空闲期间不累计 SKP，恢复时建立新的数据流。 */
TEST(LinkSerializer, ElectricalIdleSuspendsSkip)
{
    Serializer serializer(4, 4);
    serializer.beginStream(0);
    auto idle = serializer.electricalIdle(0);
    EXPECT_GT(idle.orderedHalf, 0);
    EXPECT_FALSE(serializer.state().active);
    auto training = serializer.training(8, 1000000000);
    EXPECT_EQ(training.skpCount, 0);
    EXPECT_EQ(training.endTick() - training.beginTick(), 8 * 8125);
}

/** 排空后的恢复保留下一包的相位和 SKP 时刻。 */
TEST(LinkSerializer, RestorePhase)
{
    Serializer original(3, 2);
    original.beginStream(0);
    original.packet(20, 0);
    auto state = original.state();
    Serializer restored(1, 1);
    restored.restore(state);
    EXPECT_EQ(original.packet(152, 0).endHalf,
              restored.packet(152, 0).endHalf);
    EXPECT_EQ(original.nextSkpTick(), restored.nextSkpTick());
}

/** 非法模式和数据流状态由调用边界明确拒绝。 */
TEST(LinkSerializer, RejectInvalidCalls)
{
    EXPECT_THROW(Serializer(5, 4), std::invalid_argument);
    EXPECT_THROW(Serializer(4, 3), std::invalid_argument);
    Serializer serializer(4, 4);
    EXPECT_THROW(serializer.packet(20, 0), std::invalid_argument);
    serializer.beginStream(0);
    EXPECT_THROW(serializer.packet(3, 0), std::invalid_argument);
    EXPECT_THROW(serializer.training(8, 0), std::invalid_argument);
}
