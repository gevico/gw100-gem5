/**
 * @file  ct_nidma.test.cc
 * @brief CtNidma 的 64B 切片、跨 256B 边界对齐与重排写回单元测试
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "src/gw100/ct/ct_windows.hh"
#include "src/gw100/ct/ctrl/ct_ctrl_ram.hh"
#include "src/gw100/ct/nidma/ct_nidma.hh"
#include "src/gw100/ct/sram/ct_sram.hh"
#include "src/gw100/ct/sync/ct_sync.hh"

using namespace gem5;
using namespace gem5::gw100;

namespace
{

class TestClock : public CtClockIf
{
  public:
    Tick curTick = 1000;
    Tick clkPeriod = 1000;
    Tick now() const override { return curTick; }
    Tick period() const override { return clkPeriod; }
    void wakeAt(Tick when) override { curTick = when; }
};

/** 模拟外部 NoC 与设备内存：强制校验每次请求 <= 64B 且不跨 256B 边界 */
class FakeNoc : public CtNocIf
{
  public:
    std::vector<uint8_t> dram;
    std::vector<unsigned> sliceLengths;

    FakeNoc() : dram(4096, 0) {}

    bool canSend() const override { return true; }

    void sendRead(Addr dpa, unsigned len, CtNocReadDone done) override
    {
        EXPECT_LE(len, NocPacketLimit);
        EXPECT_EQ(dpa / 256, (dpa + len - 1) / 256); // 不跨 256B 边界
        sliceLengths.push_back(len);
        done(CtStatus::Ok, &dram[dpa]);
    }

    void sendWrite(Addr dpa, unsigned len, const uint8_t *data,
                   CtNocWriteDone done) override
    {
        EXPECT_LE(len, NocPacketLimit);
        EXPECT_EQ(dpa / 256, (dpa + len - 1) / 256);
        sliceLengths.push_back(len);
        std::memcpy(&dram[dpa], data, len);
        done(CtStatus::Ok);
    }
};

} // anonymous namespace

/** 测试跨 256B 边界的读入重排（0xF0 起搬 100 字节 -> 应切成 16B + 64B + 20B）与写出 */
TEST(CtNidmaTest, SliceReassembleAndRoundTrip)
{
    TestClock clk;
    FakeNoc noc;
    CtSram sram(clk);
    CtSync sync(clk);
    CtCtrlSram ctrl_ram(clk);
    CtWindow win(sram, sync, ctrl_ram);
    CtNidma nidma(clk, noc, win);

    // 1. 在外部 FakeNoc 的 0xF0 处准备 100 字节递增数据
    for (unsigned i = 0; i < 100; ++i) {
        noc.dram[0xF0 + i] = static_cast<uint8_t>(i + 1);
    }

    // 2. 发起读任务：从外部 0xF0 搬 100 字节到本地 SRAM 0x200
    CtStatus final_st = CtStatus::Fault;
    CtNidmaDesc rd_desc{0xF0, 0x200, 100, true};
    EXPECT_TRUE(nidma.subDma(rd_desc, [&](CtStatus st) { final_st = st; }));
    EXPECT_EQ(final_st, CtStatus::Ok);
    EXPECT_FALSE(nidma.busy());

    // 验证切片序列严格为：16B (到 0x100 边界) -> 64B -> 20B
    ASSERT_EQ(noc.sliceLengths.size(), 3u);
    EXPECT_EQ(noc.sliceLengths[0], 16u);
    EXPECT_EQ(noc.sliceLengths[1], 64u);
    EXPECT_EQ(noc.sliceLengths[2], 20u);

    // 3. 发起写任务：把刚才写进本地 SRAM 0x200 的 100 字节搬回外部 FakeNoc 的 0x400
    CtNidmaDesc wr_desc{0x400, 0x200, 100, false};
    final_st = CtStatus::Fault;
    EXPECT_TRUE(nidma.subDma(wr_desc, [&](CtStatus st) { final_st = st; }));
    EXPECT_EQ(final_st, CtStatus::Ok);

    for (unsigned i = 0; i < 100; ++i) {
        EXPECT_EQ(noc.dram[0x400 + i], static_cast<uint8_t>(i + 1));
    }
}
