/**
 * @file  mgmt_crc.test.cc
 * @brief MgmtCrc 的单元测试
 *
 * 检查复位值、RESET_CAUSE 的 W1C、MESH_RST 只能由 1 写为 0、PLL 的
 * 锁定时间，以及 CLK_SEL 只能切到已锁定的 PLL、不能切回、写应答推迟到
 * 切换完成（spec-clock-reset-power §3.4、§6）。
 *
 * @see spec-clock-reset-power
 */

#include <gtest/gtest.h>

#include "src/gw100/mgmt/crc/mgmt_crc.hh"

using namespace gem5;
using namespace gem5::gw100;

namespace
{

/** 1 ns 对应的 tick 数 */
constexpr Tick ns = 1000;
/** 测试用的时序参数 */
const MgmtCrc::Config cfg{100'000 * ns, 200 * ns, 84 * ns};

/** 读一个寄存器，失败时让测试失败 */
uint32_t
rd(const MgmtCrc &c, Addr off, Tick now)
{
    uint32_t v = 0;
    EXPECT_TRUE(c.read(off, v, now));
    return v;
}

} // anonymous namespace

TEST(MgmtCrc, ResetValues)
{
    MgmtCrc c(cfg, 0);
    EXPECT_EQ(rd(c, 0x000, 0), 1u);         // POR
    EXPECT_EQ(rd(c, 0x010, 0), 0x1Fu);
    EXPECT_EQ(rd(c, 0x014, 0), 0xFFFFu);
    EXPECT_EQ(rd(c, 0x018, 0), 0u);
    EXPECT_EQ(rd(c, 0x01C, 0), 0xFFFFu);
    EXPECT_EQ(rd(c, 0x038, 0), 0u);
    EXPECT_EQ(rd(c, 0x03C, 0), 0u);
    uint32_t v;
    EXPECT_FALSE(c.read(0x040, v, 0));
    EXPECT_EQ(c.write(0x040, 0, 0), MaxTick);
}

TEST(MgmtCrc, W1cAndClearOnly)
{
    MgmtCrc c(cfg, 0);
    EXPECT_EQ(c.write(0x000, 1, 0), cfg.access);
    EXPECT_EQ(rd(c, 0x000, 0), 0u);
    c.write(0x010, 0x1E, 0);                // 释放 NoC
    EXPECT_EQ(rd(c, 0x010, 0), 0x1Eu);
    c.write(0x010, 0x1F, 0);                // 不能重新置 1
    EXPECT_EQ(rd(c, 0x010, 0), 0x1Eu);
}

TEST(MgmtCrc, ExternalResetCauses)
{
    MgmtCrc c(cfg, 0);
    c.devReset(2);
    EXPECT_EQ(rd(c, 0, 0), 2u);
    c.write(0, 2, 0);
    c.devReset(4);
    c.devReset(8);
    EXPECT_EQ(rd(c, 0, 0), 12u);
    c.devReset(1);
    EXPECT_EQ(rd(c, 0, 0), 1u);
    EXPECT_EQ(rd(c, 0x10, 0), 0x1Fu);
}

TEST(MgmtCrc, PllTsLocksAfterReset)
{
    MgmtCrc c(cfg, 10 * ns);
    EXPECT_EQ(rd(c, 0x038, 10 * ns + cfg.pllLock - 1) & 4, 0u);
    EXPECT_EQ(rd(c, 0x038, 10 * ns + cfg.pllLock) & 4, 4u);
}

TEST(MgmtCrc, PllCoreLock)
{
    MgmtCrc c(cfg, 0);
    Tick done = c.write(0x030, 1, 0);
    EXPECT_EQ(rd(c, 0x038, done + cfg.pllLock - 1) & 1, 0u);
    EXPECT_EQ(rd(c, 0x038, done + cfg.pllLock) & 1, 1u);
    c.write(0x030, 0, done + cfg.pllLock);  // 关闭后失锁
    EXPECT_EQ(rd(c, 0x038, done + 2 * cfg.pllLock) & 1, 0u);
}

TEST(MgmtCrc, ClkSelNeedsLockedPll)
{
    MgmtCrc c(cfg, 0);
    int calls = 0;
    c.onMgmtSwitch = [&](Tick) { ++calls; };
    EXPECT_EQ(c.write(0x03C, 3, 0), cfg.access);  // 未锁定：忽略
    EXPECT_EQ(rd(c, 0x03C, cfg.access), 0u);
    EXPECT_EQ(calls, 0);
}

TEST(MgmtCrc, ClkSelSwitchAndNoReturn)
{
    MgmtCrc c(cfg, 0);
    Tick sw = 0;
    int calls = 0;
    c.onMgmtSwitch = [&](Tick t) { sw = t; ++calls; };
    Tick t = c.write(0x030, 1, 0) + cfg.pllLock;
    Tick done = c.write(0x03C, 3, t);
    EXPECT_EQ(done, t + cfg.access + cfg.switchPause);
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(sw, done);
    EXPECT_EQ(rd(c, 0x03C, done - 1), 0u);  // 切换完成前读回旧值
    EXPECT_EQ(rd(c, 0x03C, done), 3u);
    // 不能切回 XTAL，也不再触发切换
    EXPECT_EQ(c.write(0x03C, 0, done), done + cfg.access);
    EXPECT_EQ(rd(c, 0x03C, done + cfg.access), 3u);
    EXPECT_EQ(calls, 1);
}

TEST(MgmtCrc, CoreOnlySwitchKeepsMgmt)
{
    MgmtCrc c(cfg, 0);
    int calls = 0;
    c.onMgmtSwitch = [&](Tick) { ++calls; };
    Tick t = c.write(0x030, 1, 0) + cfg.pllLock;
    Tick done = c.write(0x03C, 1, t);
    EXPECT_EQ(rd(c, 0x03C, done), 1u);
    EXPECT_EQ(calls, 0);
    done = c.write(0x03C, 2, done);         // 再切 MGMT，CORE 保持
    EXPECT_EQ(rd(c, 0x03C, done), 3u);
    EXPECT_EQ(calls, 1);
}
