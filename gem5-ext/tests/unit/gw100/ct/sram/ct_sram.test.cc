/**
 * @file  ct_sram.test.cc
 * @brief CtSram 的读写、Bank 排队时序与 256B 边界单元测试
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "src/gw100/ct/sram/ct_sram.hh"

using namespace gem5;
using namespace gem5::gw100;

namespace
{

class TestClock : public CtClockIf
{
  public:
    Tick curTick = 0;
    Tick clkPeriod = 1000;

    Tick now() const override { return curTick; }
    Tick period() const override { return clkPeriod; }
    void wakeAt(Tick) override {}
};

} 

/** 测试正常读写、同 Bank 排队累加延迟、不同 Bank 并发以及 RMW 惩罚 */
TEST(CtSramTest, ReadWriteAndBankSchedule)
{
    TestClock clk;
    CtSram sram(clk);

    // 1. 往 Bank 0 (偏移 0x00) 对齐写入 16 字节 (2 个 8B Word)，预期耗时 2 拍 = 2000
    std::vector<uint8_t> wr_buf(16, 0x5A);
    CtAccess a{0x00, wr_buf.data(), true, CtInitiator::LocalEngine, 16};
    CtAccessResult res1 = sram.access(a, 1000);
    EXPECT_EQ(res1.status, CtStatus::Ok);
    EXPECT_EQ(res1.delay, 2000u);

    // 2. 同一时刻 t=1000 再次访问同一个 Bank 0 (偏移 0x10)，读 8 字节 (1 拍)
    // 因前一次占用到 3000，本次完成时刻为 4000，delay = 4000 - 1000 = 3000
    std::vector<uint8_t> rd_buf(16, 0);
    CtAccess rd{0x00, rd_buf.data(), false, CtInitiator::LocalEngine, 16};
    CtAccessResult res2 = sram.access(rd, 1000);
    EXPECT_EQ(res2.status, CtStatus::Ok);
    EXPECT_EQ(res2.delay, 4000u);
    EXPECT_EQ(rd_buf, wr_buf);

    // 3. 同一时刻 t=1000 访问 Bank 1 (偏移 0x100 = 256B)，互不冲突，8B 读仅需 1 拍 = 1000
    CtAccess rd_bank1{0x100, rd_buf.data(), false, CtInitiator::LocalEngine, 8};
    CtAccessResult res3 = sram.access(rd_bank1, 1000);
    EXPECT_EQ(res3.status, CtStatus::Ok);
    EXPECT_EQ(res3.delay, 1000u);

    // 4. 非 8B 整字写 (RMW 惩罚)：在 Bank 2 (偏移 0x200) 写 4 字节，占 1 个字 + 1 拍 RMW = 2 拍
    CtAccess rmw_wr{0x200, wr_buf.data(), true, CtInitiator::LocalEngine, 4};
    CtAccessResult res_rmw = sram.access(rmw_wr, 1000);
    EXPECT_EQ(res_rmw.status, CtStatus::Ok);
    EXPECT_EQ(res_rmw.delay, 2000u);
}

/** 测试越界、跨 256B Bank 边界拒绝与非法参数 Fault */
TEST(CtSramTest, BoundaryAndInvalidRejected)
{
    TestClock clk;
    CtSram sram(clk);
    std::vector<uint8_t> buf(300, 1);

    // 1. 空指针或长度为 0 -> Fault
    CtAccess fault_acc{0, nullptr, false, CtInitiator::LocalEngine, 8};
    EXPECT_EQ(sram.access(fault_acc, 0).status, CtStatus::Fault);

    // 2. 单次超过 256B 或跨越 256B 边界 (如从 0xF8 写 16 字节跨入 0x100) -> Reject
    CtAccess cross_bank{0xF8, buf.data(), true, CtInitiator::LocalEngine, 16};
    EXPECT_EQ(sram.access(cross_bank, 0).status, CtStatus::Reject);

    // 3. 超过 2 MiB 容量上限 -> Reject
    CtAccess out_of_range{CtSramSize - 4, buf.data(), false,
                          CtInitiator::LocalEngine, 8};
    EXPECT_EQ(sram.access(out_of_range, 0).status, CtStatus::Reject);
}
