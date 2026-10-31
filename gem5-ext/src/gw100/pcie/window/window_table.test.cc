/**
 * @file  window_table.test.cc
 * @brief WindowTable 的单元测试
 *
 * @see spec-pcie
 */

#include <gtest/gtest.h>

#include "src/gw100/pcie/window/window_table.hh"

using namespace gem5;
using namespace gem5::gw100;

namespace
{

/** 每个窗口 4 MiB */
constexpr Addr WinSize = 0x400000;
/** 设备内存 32 GiB，起始 DPA 0 */
constexpr Addr DevmemSize = 0x800000000ull;

/** 16 个 4 MiB 窗口，DPA 宽 40 位 */
class WindowTableTest : public ::testing::Test
{
  protected:
    /** 被测的窗口表 */
    WindowTable t{16, WinSize, 0, DevmemSize, 40};
};

} // anonymous namespace

/** 复位后窗口未使能，访问报 Disabled */
TEST_F(WindowTableTest, DisabledAfterReset)
{
    EXPECT_EQ(t.ctrl(0), 0u);
    EXPECT_EQ(t.translate(0x10, 4, false).fault,
              WindowTable::Fault::Disabled);
}

/** 寄存器字段的掩码：CTRL 两位、基址 4 MiB 对齐、高位按 40 位截断 */
TEST_F(WindowTableTest, FieldMasks)
{
    t.setCtrl(7, 0xFFFFFFFF);
    t.setBaseLo(7, 0xFFFFFFFF);
    t.setBaseHi(7, 0xFFFFFFFF);
    EXPECT_EQ(t.ctrl(7), WindowTable::CtrlEn | WindowTable::CtrlRdOnly);
    EXPECT_EQ(t.baseLo(7), 0xFFC00000u);
    EXPECT_EQ(t.baseHi(7), 0xFFu);
}

/** 使能的窗口：BAR2 偏移转换为基址加窗口内偏移 */
TEST_F(WindowTableTest, Translate)
{
    t.setBaseLo(1, 0x10000000);
    t.setCtrl(1, WindowTable::CtrlEn);
    const auto r = t.translate(WinSize + 0x100, 4, false);
    EXPECT_EQ(r.fault, WindowTable::Fault::None);
    EXPECT_EQ(r.target, 0x10000100u);
}

/** 目标超出设备内存、或访问跨过设备内存末尾，报 Target */
TEST_F(WindowTableTest, TargetOutOfRange)
{
    t.setBaseLo(2, uint32_t(DevmemSize));
    t.setBaseHi(2, uint32_t(DevmemSize >> 32));
    t.setCtrl(2, WindowTable::CtrlEn);
    EXPECT_EQ(t.translate(2 * WinSize, 4, false).fault,
              WindowTable::Fault::Target);

    const Addr last = DevmemSize - WinSize;
    t.setBaseLo(3, uint32_t(last));
    t.setBaseHi(3, uint32_t(last >> 32));
    t.setCtrl(3, WindowTable::CtrlEn);
    EXPECT_EQ(t.translate(3 * WinSize + WinSize - 4, 4, false).fault,
              WindowTable::Fault::None);
    EXPECT_EQ(t.translate(3 * WinSize + WinSize - 2, 4, false).fault,
              WindowTable::Fault::Target);
}

/** 只读窗口：写报 Target，读正常 */
TEST_F(WindowTableTest, ReadOnly)
{
    t.setCtrl(5, WindowTable::CtrlEn | WindowTable::CtrlRdOnly);
    EXPECT_EQ(t.translate(5 * WinSize, 4, true).fault,
              WindowTable::Fault::Target);
    EXPECT_EQ(t.translate(5 * WinSize, 4, false).fault,
              WindowTable::Fault::None);
}

/** reset() 把全部窗口恢复为未使能、基址 0 */
TEST_F(WindowTableTest, Reset)
{
    t.setBaseLo(4, 0x10000000);
    t.setCtrl(4, WindowTable::CtrlEn);
    t.reset();
    EXPECT_EQ(t.ctrl(4), 0u);
    EXPECT_EQ(t.baseLo(4), 0u);
}
