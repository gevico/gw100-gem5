/**
 * @file  mgmt_wdt.test.cc
 * @brief MgmtWdt 的单元测试
 *
 * 按 spec-mgmt-tile §7.4 检查看门狗的状态机：计时基准与每次最多到期
 * 一次、LOCK 按写入前的值判定、喂狗的状态转移表、EN = 0 与 STAGE2
 * 之后的写入、WDT_LOAD = 0 与访问宽度。
 *
 * @see spec-mgmt-tile
 */

#include <gtest/gtest.h>

#include "src/gw100/mgmt/wdt/mgmt_wdt.hh"

using namespace gem5;
using namespace gem5::gw100;

namespace
{

/** WDT_CTRL 的偏移 */
constexpr Addr Ctrl = 0x00;
/** WDT_LOAD 的偏移 */
constexpr Addr Load = 0x08;
/** WDT_KICK 的偏移 */
constexpr Addr Kick = 0x10;
/** WDT_STATUS 的偏移 */
constexpr Addr Status = 0x14;
/** WDT_CTRL.EN */
constexpr uint32_t En = 1;
/** WDT_CTRL.LOCK */
constexpr uint32_t Lock = 2;

/** 读一个寄存器，失败时让测试失败 */
uint64_t
rd(const MgmtWdt &w, Addr off)
{
    uint64_t v = 0;
    EXPECT_TRUE(w.read(off, off == Load ? 8 : 4, v));
    return v;
}

/** 在样本 s 写一个寄存器，失败时让测试失败 */
void
wr(MgmtWdt &w, Addr off, uint64_t v, uint64_t s)
{
    EXPECT_TRUE(w.write(off, off == Load ? 8 : 4, v, s));
}

} // anonymous namespace

TEST(MgmtWdt, ResetAndWidths)
{
    MgmtWdt w;
    EXPECT_EQ(rd(w, Ctrl), 0u);
    EXPECT_EQ(rd(w, Load), 0u);
    EXPECT_EQ(rd(w, Kick), 0u);
    EXPECT_EQ(rd(w, Status), 0u);
    EXPECT_FALSE(w.counting());
    uint64_t v;
    EXPECT_FALSE(w.read(Load, 4, v));
    EXPECT_FALSE(w.read(Ctrl, 8, v));
    EXPECT_FALSE(w.read(0x18, 4, v));
    EXPECT_FALSE(w.write(Load, 4, 1, 0));
}

TEST(MgmtWdt, TwoStages)
{
    MgmtWdt w;
    wr(w, Load, 100, 0);
    wr(w, Ctrl, En | Lock, 10);
    EXPECT_EQ(w.deadline(), 110u);
    w.evaluate(109);
    EXPECT_FALSE(w.stage1());
    w.evaluate(110);
    EXPECT_TRUE(w.stage1());
    EXPECT_EQ(w.deadline(), 210u);
    w.evaluate(209);
    EXPECT_FALSE(w.stage2());
    w.evaluate(210);
    EXPECT_TRUE(w.stage2());
    EXPECT_TRUE(w.stage1());        // 第二级之后 STAGE1 保持
    EXPECT_FALSE(w.counting());
    EXPECT_EQ(rd(w, Status), 3u);
}

TEST(MgmtWdt, AtMostOneExpiryAndNoCatchUp)
{
    MgmtWdt w;
    wr(w, Load, 10, 0);
    wr(w, Ctrl, En, 0);
    w.evaluate(35);                 // 跨过三个周期也只到期一次
    EXPECT_TRUE(w.stage1());
    EXPECT_FALSE(w.stage2());
    EXPECT_EQ(w.deadline(), 45u);   // T0 ← S，不追补
}

TEST(MgmtWdt, LoadZeroIsOne)
{
    MgmtWdt w;
    wr(w, Ctrl, En, 5);
    EXPECT_EQ(w.deadline(), 6u);
    w.evaluate(6);
    EXPECT_TRUE(w.stage1());
}

TEST(MgmtWdt, LockJudgedBeforeWrite)
{
    MgmtWdt w;
    wr(w, Load, 50, 0);
    wr(w, Ctrl, En | Lock, 0);      // 同一次写入，两者都生效
    EXPECT_EQ(rd(w, Ctrl), uint64_t(En | Lock));
    wr(w, Ctrl, 0, 10);             // 锁定后不能关闭
    EXPECT_EQ(rd(w, Ctrl), uint64_t(En | Lock));
    wr(w, Load, 1000, 20);          // 锁定后不能改，也不重设 T0
    EXPECT_EQ(rd(w, Load), 50u);
    EXPECT_EQ(w.deadline(), 50u);
}

TEST(MgmtWdt, UnlockedLoadAndDisable)
{
    MgmtWdt w;
    wr(w, Load, 10, 0);
    wr(w, Ctrl, En, 0);
    w.evaluate(10);
    EXPECT_TRUE(w.stage1());
    wr(w, Load, 20, 15);            // 写 LOAD 重设 T0，不清除 STAGE1
    EXPECT_TRUE(w.stage1());
    EXPECT_EQ(w.deadline(), 35u);
    wr(w, Ctrl, 0, 16);             // 关闭时清除 STAGE1，停止计时
    EXPECT_FALSE(w.stage1());
    EXPECT_FALSE(w.counting());
    wr(w, Ctrl, En, 100);           // 重新使能时 T0 ← S
    EXPECT_EQ(w.deadline(), 120u);
}

TEST(MgmtWdt, LegalKick)
{
    MgmtWdt w;
    wr(w, Load, 10, 0);
    wr(w, Ctrl, En, 0);
    wr(w, Kick, MgmtWdt::KickMagic, 9);
    EXPECT_EQ(w.deadline(), 19u);
    w.evaluate(19);
    EXPECT_TRUE(w.stage1());
    wr(w, Kick, MgmtWdt::KickMagic, 20);
    EXPECT_FALSE(w.stage1());
    EXPECT_EQ(w.deadline(), 30u);
}

TEST(MgmtWdt, KickSameCycleAsExpiry)
{
    MgmtWdt w;
    wr(w, Load, 10, 0);
    wr(w, Ctrl, En, 0);
    wr(w, Kick, MgmtWdt::KickMagic, 10);  // 本应到期的周期：先处理写入
    EXPECT_FALSE(w.stage1());
    w.evaluate(10);
    EXPECT_FALSE(w.stage1());
}

TEST(MgmtWdt, IllegalKick)
{
    MgmtWdt w;
    wr(w, Load, 10, 0);
    wr(w, Ctrl, En, 0);
    wr(w, Kick, 1, 3);
    EXPECT_TRUE(w.stage1());
    EXPECT_EQ(w.deadline(), 13u);
    wr(w, Kick, 0, 13);             // 与自然到期同周期：只转移一次
    EXPECT_TRUE(w.stage2());
    EXPECT_EQ(rd(w, Status), 3u);
}

TEST(MgmtWdt, KickIgnoredWhenDisabled)
{
    MgmtWdt w;
    wr(w, Load, 10, 0);
    wr(w, Kick, 1, 5);
    EXPECT_EQ(rd(w, Status), 0u);
    wr(w, Ctrl, En, 7);
    EXPECT_EQ(w.deadline(), 17u);
}

TEST(MgmtWdt, WritesIgnoredAfterStage2)
{
    MgmtWdt w;
    wr(w, Load, 10, 0);
    wr(w, Ctrl, En, 0);
    wr(w, Kick, 1, 1);
    wr(w, Kick, 1, 2);
    ASSERT_TRUE(w.stage2());
    wr(w, Kick, MgmtWdt::KickMagic, 3);
    wr(w, Ctrl, 0, 4);
    wr(w, Load, 99, 5);
    EXPECT_EQ(rd(w, Status), 3u);
    EXPECT_EQ(rd(w, Ctrl), uint64_t(En));
    EXPECT_EQ(rd(w, Load), 10u);
}
