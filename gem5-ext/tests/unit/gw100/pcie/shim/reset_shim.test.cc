/**
 * @file  reset_shim.test.cc
 * @brief ResetShim 的单元测试
 *
 * 按 spec-clock-reset-power §4.3 第 4 步检查 reset shim 的响应：
 * DEV_VERSION、DEV_CAPS 和 DEV_STATUS 的链路字段为真实值，BOOT_STATUS
 * 与 BAR0 其他偏移为 0，BAR2 读为全 1，写入没有副作用。
 *
 * @see spec-clock-reset-power
 */

#include <gtest/gtest.h>

#include <cstring>

#include "src/gw100/pcie/shim/reset_shim.hh"

using namespace gem5;
using namespace gem5::gw100;

namespace
{

/** 测试用的 shim：版本 0x100，能力 0x10203010，链路字段 0x44 */
ResetShim
make()
{
    return ResetShim(0x100, 0x10203010, 0x44, 1000);
}

/** 读 BAR bar 偏移 off 处 size 字节 */
uint64_t
rd(const ResetShim &s, int bar, Addr off, unsigned size)
{
    uint8_t buf[8] = {0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA};
    BarAccess a{off, size, false, buf};
    EXPECT_EQ(s.access(bar, a), 1000u);
    uint64_t v = 0;
    std::memcpy(&v, buf, size);
    return v;
}

} // anonymous namespace

TEST(ResetShim, EnterExit)
{
    ResetShim s = make();
    EXPECT_FALSE(s.active());
    s.enter();
    EXPECT_TRUE(s.active());
    s.exit();
    EXPECT_FALSE(s.active());
}

TEST(ResetShim, Bar0RealValues)
{
    ResetShim s = make();
    EXPECT_EQ(rd(s, 0, 0x000, 4), 0x100u);
    EXPECT_EQ(rd(s, 0, 0x004, 4), 0x10203010u);
    EXPECT_EQ(rd(s, 0, 0x008, 4), 0x44u);         // DEV_FATAL 等位为 0
    EXPECT_EQ(rd(s, 0, 0x000, 8), 0x10203010'00000100ull);
    EXPECT_EQ(rd(s, 0, 0x005, 1), 0x30u);
}

TEST(ResetShim, Bar0OthersZero)
{
    ResetShim s = make();
    EXPECT_EQ(rd(s, 0, 0x00C, 4), 0u);            // SCRATCH
    EXPECT_EQ(rd(s, 0, 0x010, 8), 0u);            // 时间戳
    EXPECT_EQ(rd(s, 0, 0x4000, 4), 0u);           // BOOT_STATUS
    EXPECT_EQ(rd(s, 0, 0x5000, 4), 0u);           // 未实现的块：不报错
}

TEST(ResetShim, Bar2AllOnes)
{
    ResetShim s = make();
    EXPECT_EQ(rd(s, 2, 0x0, 8), ~0ull);
    EXPECT_EQ(rd(s, 2, 0x100, 4), 0xFFFFFFFFu);
}

TEST(ResetShim, WritesDropped)
{
    ResetShim s = make();
    uint8_t buf[4] = {1, 2, 3, 4};
    BarAccess a{0x00C, 4, true, buf};
    EXPECT_EQ(s.access(0, a), 1000u);
    EXPECT_EQ(buf[0], 1);                          // 缓冲不被改写
    EXPECT_EQ(rd(s, 0, 0x00C, 4), 0u);
}
