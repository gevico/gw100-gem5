/**
 * @file  ct_sync.test.cc
 * @brief Compute Tile 的测试
 *
 * @see spec- ct
 */
 #include <gtest/gtest.h>

 #include "src/gw100/ct/sync/ct_sync.hh"

 using namespace gem5;
 using namespace gem5::gw100;

 namespace
{
/** 测试时钟 */
class TestClock : public CtClockIf
{
    public:
    Tick curTick = 0;
    /** 假设，后续可能需要进行同喜 */
    Tick clkPeriod = 1000; 

    Tick now() const override { return curTick; };
    Tick period() const override { return clkPeriod; };
    void wakeAt(Tick) override {};
};
}

/** sync 内部接口测试 */
TEST(CtSyncTest, SignalAndTryWait)
{
    TestClock clk;
    CtSync sync(clk);

    EXPECT_FALSE(sync.tryWait(0, 1));
    EXPECT_TRUE(sync.signal(0, 3));
    EXPECT_TRUE(sync.tryWait(0, 2));
    EXPECT_FALSE(sync.tryWait(0, 2));
    EXPECT_TRUE(sync.tryWait(0, 1));
    EXPECT_FALSE(sync.tryWait(0, 1));
    /** 边界测试 */
    EXPECT_FALSE(sync.signal(CtSync::NumCounters, 1));
    EXPECT_FALSE(sync.tryWait(CtSync::NumCounters, 1));
    EXPECT_TRUE(sync.signal(1, std::numeric_limits<uint32_t>::max()));
    EXPECT_FALSE(sync.signal(1, 1));
    EXPECT_TRUE(sync.tryWait(1, std::numeric_limits<uint32_t>::max()));
}

/** sync 外部 access 接口测试：覆盖累加写、读回、同计数器排队时序、不同计数器并发与 reset */
TEST(CtSyncTest, WindowAccessAndSchedule)
{
    TestClock clk;
    CtSync sync(clk);

    uint32_t write_val = 5;
    CtAccess a{};
    a.base = CtSyncBase + 4;
    a.data = reinterpret_cast<uint8_t *>(&write_val);
    a.write = true;
    a.initiator = CtInitiator::LocalCpu;
    a.size = 4;

    CtAccessResult res1 = sync.access(a, 1000);
    EXPECT_EQ(res1.status, CtStatus::Ok);
    // 端口空闲，耗时 1 个周期（1000 tick），完成时刻为 2000，delay = 2000 - 1000 = 1000
    EXPECT_EQ(res1.delay, 1000u);

    write_val = 3;
    CtAccessResult res2 = sync.access(a, 1000);
    EXPECT_EQ(res2.status, CtStatus::Ok);
    // 1 号计数器要到 2000 才空闲，本次从 2000 排到 3000，所以相对于 now=1000 的 delay 是 2000
    EXPECT_EQ(res2.delay, 2000u);

    CtAccess a_other = a;
    a_other.base = CtSyncBase + 8;
    write_val = 7;
    CtAccessResult res_other = sync.access(a_other, 1000);
    EXPECT_EQ(res_other.status, CtStatus::Ok);
    EXPECT_EQ(res_other.delay, 1000u);

    uint32_t read_val = 0;
    CtAccess rd{};
    rd.base = CtBase + CtWindowSize + CtSyncBase + 4; // 顺带验证带全局 Tile 窗口基址的地址归一化
    rd.data = reinterpret_cast<uint8_t *>(&read_val);
    rd.write = false;
    rd.initiator = CtInitiator::LocalCpu;
    rd.size = 4;

    CtAccessResult res_rd = sync.access(rd, 3000);
    EXPECT_EQ(res_rd.status, CtStatus::Ok);
    EXPECT_EQ(res_rd.delay, 1000u);
    EXPECT_EQ(read_val, 8u);

    sync.reset();
    read_val = 99;
    CtAccessResult res_after_rst = sync.access(rd, 500);
    EXPECT_EQ(res_after_rst.status, CtStatus::Ok);
    EXPECT_EQ(res_after_rst.delay, 1000u);
    EXPECT_EQ(read_val, 0u);
}

/** sync 非法访问与边界拒绝测试：覆盖 Fault、地址越界、非 4B 对齐、粒度错误与写溢出 Reject */
TEST(CtSyncTest, InvalidAccessRejected)
{
    TestClock clk;
    CtSync sync(clk);
    uint32_t val = 1;

    CtAccess bad_fault{CtSyncBase, nullptr, true, CtInitiator::LocalCpu, 4};
    EXPECT_EQ(sync.access(bad_fault, 0).status, CtStatus::Fault);
    bad_fault.data = reinterpret_cast<uint8_t *>(&val);
    bad_fault.size = 0;
    EXPECT_EQ(sync.access(bad_fault, 0).status, CtStatus::Fault);
    CtAccess out_of_range{CtSyncBase - 4, reinterpret_cast<uint8_t *>(&val),
                          false, CtInitiator::LocalCpu, 4};
    EXPECT_EQ(sync.access(out_of_range, 0).status, CtStatus::Reject);

    out_of_range.base = CtSyncBase + CtSyncSize; // 刚好越过第 31 号计数器末尾
    EXPECT_EQ(sync.access(out_of_range, 0).status, CtStatus::Reject);
    CtAccess bad_align{CtSyncBase + 2, reinterpret_cast<uint8_t *>(&val),
                       true, CtInitiator::LocalCpu, 4};
    EXPECT_EQ(sync.access(bad_align, 0).status, CtStatus::Reject);

    CtAccess bad_size{CtSyncBase, reinterpret_cast<uint8_t *>(&val),
                      true, CtInitiator::LocalCpu, 2};
    EXPECT_EQ(sync.access(bad_size, 0).status, CtStatus::Reject);

    uint32_t max_val = std::numeric_limits<uint32_t>::max();
    CtAccess wr_max{CtSyncBase, reinterpret_cast<uint8_t *>(&max_val),
                    true, CtInitiator::LocalCpu, 4};
    EXPECT_EQ(sync.access(wr_max, 1000).status, CtStatus::Ok);
    uint32_t extra = 1;
    CtAccess wr_overflow{CtSyncBase, reinterpret_cast<uint8_t *>(&extra),
                         true, CtInitiator::LocalCpu, 4};
    EXPECT_EQ(sync.access(wr_overflow, 2000).status, CtStatus::Reject);
    EXPECT_TRUE(sync.tryWait(0, max_val));
}
