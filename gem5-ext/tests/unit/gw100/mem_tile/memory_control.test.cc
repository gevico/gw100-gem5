/**
 * @file  memory_control.test.cc
 * @brief Memory Tile 寄存器、初始化与保护边界验证
 *
 * @see spec-dram, spec-address-map
 */

#include <gtest/gtest.h>

#include "src/gw100/mem_tile/memory_control.hh"

using gem5::gw100::MemoryControl;
namespace md = gem5::gw100::memorydefs;

namespace
{
/** 通过控制器命令和反馈推进到 TEST */
void
train(MemoryControl &c, unsigned ch)
{
    c.released = c.pllLocked = true;
    c.channels[ch].profile = md::PROFILE_ID;
    ASSERT_EQ(c.command(ch, md::CMD_CONFIGURE), md::ERR_NONE);
    ASSERT_EQ(c.command(ch, md::CMD_CALIBRATE), md::ERR_NONE);
    c.complete(ch, md::STATE_CALIBRATED);
    ASSERT_EQ(c.command(ch, md::CMD_TRAIN), md::ERR_NONE);
    c.channels[ch].trained = 3;
    c.complete(ch, md::STATE_TEST);
}
} // namespace

TEST(MemoryControl, ResetAndWidths)
{
    MemoryControl c(3);
    uint64_t value = 0;
    EXPECT_TRUE(c.read(md::REG_VERSION, 4, value));
    EXPECT_EQ(value, md::VERSION);
    EXPECT_FALSE(c.read(md::REG_VERSION, 8, value));
    EXPECT_FALSE(c.read(md::REG_EPOCH, 4, value));
    EXPECT_FALSE(c.read(3, 4, value));
    EXPECT_EQ(c.command(0, md::CMD_CONFIGURE), md::ERR_NOT_READY);
    EXPECT_EQ(c.channels[0].state, md::STATE_RESET);
}

TEST(MemoryControl, SequenceAndBusy)
{
    MemoryControl c(0);
    c.released = c.pllLocked = true;
    EXPECT_EQ(c.command(0, md::CMD_TRAIN), md::ERR_CONFIG);
    c.channels[0].profile = md::PROFILE_ID;
    EXPECT_EQ(c.command(0, md::CMD_CONFIGURE), md::ERR_NONE);
    EXPECT_EQ(c.command(0, md::CMD_CALIBRATE), md::ERR_NONE);
    EXPECT_EQ(c.command(0, md::CMD_TRAIN), md::ERR_BUSY);
    EXPECT_EQ(c.channels[0].sequence, 1);
    EXPECT_EQ(c.channels[0].state, md::STATE_CALIBRATING);
    c.complete(0, md::STATE_CALIBRATED);
    EXPECT_EQ(c.channels[0].sequence, 2);
    EXPECT_EQ(c.channels[0].error, 0);
}

TEST(MemoryControl, EnableRequiresTrainingAndLock)
{
    MemoryControl c(0);
    train(c, 0);
    EXPECT_EQ(c.command(0, md::CMD_ENABLE), md::ERR_CONFIG);
    ASSERT_EQ(c.write(md::REG_PROTECTION_LOCK, 4, 1), md::ERR_NONE);
    EXPECT_EQ(c.command(0, md::CMD_ENABLE), md::ERR_NONE);
    EXPECT_EQ(c.channels[0].state, md::STATE_READY);
}

TEST(MemoryControl, Lifecycle)
{
    MemoryControl c(0);
    train(c, 0);
    c.locked = true;
    ASSERT_EQ(c.command(0, md::CMD_ENABLE), md::ERR_NONE);
    ASSERT_EQ(c.command(0, md::CMD_QUIESCE), md::ERR_NONE);
    EXPECT_EQ(c.channels[0].runState, md::STATE_READY);
    c.complete(0, md::STATE_QUIESCED);
    ASSERT_EQ(c.command(0, md::CMD_SELF_REFRESH), md::ERR_NONE);
    c.complete(0, md::STATE_SELF_REFRESH);
    ASSERT_EQ(c.command(0, md::CMD_RESUME), md::ERR_NONE);
    c.complete(0, c.channels[0].runState);
    EXPECT_EQ(c.channels[0].state, md::STATE_READY);
}

TEST(MemoryControl, ProtectionBoundariesAndLock)
{
    MemoryControl c(1);
    EXPECT_EQ(c.write(md::PROTECTION_BASE, 8, 4096), md::ERR_NONE);
    EXPECT_EQ(c.write(md::PROTECTION_BASE + 8, 8, 8192), md::ERR_NONE);
    EXPECT_EQ(c.write(md::PROTECTION_BASE + 16, 4, 1), md::ERR_NONE);
    ASSERT_EQ(c.write(md::REG_PROTECTION_LOCK, 4, 1), md::ERR_NONE);
    EXPECT_TRUE(c.permitted(4096, 8, md::SOURCE_MGMT));
    EXPECT_FALSE(c.permitted(4096, 8, md::SOURCE_HDMA));
    EXPECT_FALSE(c.permitted(4092, 8, md::SOURCE_BAR2));
    EXPECT_TRUE(c.permitted(4088, 8, md::SOURCE_BAR2));
    EXPECT_TRUE(c.permitted(8192, 8, md::SOURCE_HDMA));
    EXPECT_EQ(c.write(md::PROTECTION_BASE, 8, 0), md::ERR_PERMISSION);
    EXPECT_EQ(c.write(md::REG_PROTECTION_LOCK, 4, 0), md::ERR_NONE);
    EXPECT_TRUE(c.locked);
}

TEST(MemoryControl, RejectMalformedProtection)
{
    MemoryControl c(0);
    c.regions[0] = {1, 8192, true};
    EXPECT_EQ(c.write(md::REG_PROTECTION_LOCK, 4, 1), md::ERR_CONFIG);
    EXPECT_FALSE(c.locked);
    c.regions[0] = {4096, md::TOTAL_BYTES + 4096, true};
    EXPECT_EQ(c.write(md::REG_PROTECTION_LOCK, 4, 1), md::ERR_CONFIG);
    EXPECT_FALSE(c.permitted(md::TOTAL_BYTES - 4, 8, md::SOURCE_MGMT));
    EXPECT_FALSE(c.permitted(UINT64_MAX, 8, md::SOURCE_MGMT));
}

TEST(MemoryControl, ErrorSnapshotAndFreeze)
{
    MemoryControl c(0);
    c.log(md::ERR_PERMISSION, md::SOURCE_HDMA, 4096, 7, 3);
    c.log(md::ERR_RANGE, md::SOURCE_BAR2, md::TOTAL_BYTES, 8, 4);
    EXPECT_EQ(c.errorAddr, 4096);
    EXPECT_EQ(c.errorTxn, 7);
    EXPECT_EQ(c.errors, (1u << md::ERR_PERMISSION) | (1u << md::ERR_RANGE));
    c.counters[0] = 12;
    ASSERT_EQ(c.write(md::REG_PERF_FREEZE, 4, 1), md::ERR_NONE);
    ++c.counters[0];
    uint64_t value = 0;
    ASSERT_TRUE(c.read(md::REG_REQUESTS, 8, value));
    EXPECT_EQ(value, 12);
    ASSERT_EQ(c.write(md::REG_PERF_FREEZE, 4, 0), md::ERR_NONE);
    ASSERT_TRUE(c.read(md::REG_REQUESTS, 8, value));
    EXPECT_EQ(value, 13);
}

TEST(MemoryControl, ResetEpoch)
{
    MemoryControl c(0);
    train(c, 0);
    c.locked = true;
    c.reset();
    EXPECT_EQ(c.epoch, 1);
    EXPECT_FALSE(c.released);
    EXPECT_FALSE(c.locked);
    EXPECT_EQ(c.channels[0].state, md::STATE_RESET);
    EXPECT_EQ(c.channels[0].trained, 0);
}
