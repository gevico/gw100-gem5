/**
 * @file  status_unit.test.cc
 * @brief StatusUnit 的单元测试
 *
 * @see spec-pcie
 */

#include <gtest/gtest.h>

#include "src/gw100/pcie/status/status_unit.hh"

using namespace gem5;
using namespace gem5::gw100;

/** 只锁存 ERR_STATUS 从全 0 变为非 0 时的那一次错误 */
TEST(StatusUnitTest, FirstErrorLatched)
{
    StatusUnit s("test");
    s.logError(StatusUnit::ErrRsvdAccess, 0x40, false, 4, 0);
    EXPECT_EQ(s.errStatus(), StatusUnit::ErrRsvdAccess);
    EXPECT_EQ(s.errAddr(), 0x40u);
    EXPECT_EQ(s.errInfo(), 4u << 2);
    EXPECT_TRUE(s.intStatus() & StatusUnit::IntError);

    s.logError(StatusUnit::ErrBadWidth, 0x80, true, 2, 2);
    EXPECT_EQ(s.errStatus(),
              StatusUnit::ErrRsvdAccess | StatusUnit::ErrBadWidth);
    EXPECT_EQ(s.errAddr(), 0x40u);

    s.clearErr(0xFFFFFFFF);
    s.logError(StatusUnit::ErrWinTarget, 0x1000, true, 4, 2);
    EXPECT_EQ(s.errAddr(), 0x1000u);
    EXPECT_EQ(s.errInfo(), 1u | (4u << 2) | (2u << 6));
}

/** 错误仍在时清零 INT_STATUS.ERROR，会立即重新置位 */
TEST(StatusUnitTest, ErrorReasserts)
{
    StatusUnit s("test");
    s.logError(StatusUnit::ErrRsvdAccess, 0x40, false, 4, 0);
    s.clearInt(StatusUnit::IntError);
    EXPECT_TRUE(s.intStatus() & StatusUnit::IntError);
    s.clearErr(StatusUnit::ErrRsvdAccess);
    s.clearInt(StatusUnit::IntError);
    EXPECT_FALSE(s.intStatus() & StatusUnit::IntError);
}

/** INT_SET 置位、INT_STATUS 写 1 清零、INT_MASK 与复位值 */
TEST(StatusUnitTest, IntRegistersAndReset)
{
    StatusUnit s("test");
    EXPECT_EQ(s.intMask(), 0xFFFFFFFFu);
    s.setInt(StatusUnit::IntAdmin | (1u << 16));
    s.clearInt(StatusUnit::IntAdmin);
    EXPECT_EQ(s.intStatus(), 1u << 16);
    s.setIntMask(0x0000FFFF);
    s.reset();
    EXPECT_EQ(s.intStatus(), 0u);
    EXPECT_EQ(s.intMask(), 0xFFFFFFFFu);
    EXPECT_EQ(s.errStatus(), 0u);
}
