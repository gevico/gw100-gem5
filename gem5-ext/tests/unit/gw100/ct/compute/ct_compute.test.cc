/**
 * @file  ct_compute.test.cc
 * @brief CtMatrix + CtFc32 + CtSync + CtVector 双引擎协同单元测试
 */

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "src/gw100/ct/compute/ct_fc32.hh"
#include "src/gw100/ct/compute/ct_matrix_engine.hh"
#include "src/gw100/ct/compute/ct_vector_engine.hh"
#include "src/gw100/ct/ct_windows.hh"
#include "src/gw100/ct/ctrl/ct_ctrl_ram.hh"
#include "src/gw100/ct/sram/ct_sram.hh"
#include "src/gw100/ct/sync/ct_sync.hh"

using namespace gem5;
using namespace gem5::gw100;

namespace
{

class TestClock : public CtClockIf
{
  public:
    Tick now() const override { return 1000; }
    Tick period() const override { return 1000; }
    void wakeAt(Tick) override {}
};

} // anonymous namespace

/** 测试：矩阵未算完前向量引擎因 wait_en 挂起；矩阵算完写入 FC32 并 signal 后，向量引擎从 FC32 读数完成计算写回 SRAM */
TEST(CtComputeTest, MatrixToFc32ToVectorWithSyncGate)
{
    TestClock clk;
    CtSram sram(clk);
    CtSync sync(clk);
    CtCtrlSram ctrl_ram(clk);
    CtWindow win(sram, sync, ctrl_ram);
    CtFc32 fc32;
    fc32.reset();

    CtMatrix mat(clk, win, sync, fc32);
    CtVector vec(clk, win, sync, fc32);

    // 1. 往 SRAM 0x00 写入 A (1x2) = [2.0, 3.0]，往 0x100 写入 B (2x2) = [[1.0, 2.0], [3.0, 4.0]]
    // 期望 A * B = [2*1 + 3*3, 2*2 + 3*4] = [11.0, 16.0]
    std::vector<float> A = {2.0f, 3.0f};
    std::vector<float> B = {1.0f, 2.0f, 3.0f, 4.0f};
    CtAccess wr_a{0x00, reinterpret_cast<uint8_t *>(A.data()), true,
                  CtInitiator::LocalEngine, 8};
    CtAccess wr_b{0x100, reinterpret_cast<uint8_t *>(B.data()), true,
                  CtInitiator::LocalEngine, 16};
    ASSERT_EQ(win.access(wr_a, 0).status, CtStatus::Ok);
    ASSERT_EQ(win.access(wr_b, 0).status, CtStatus::Ok);

    // 2. 准备向量描述符：等待 0 号计数器 >= 1，把 FC32[0..1] 与 FC32[0..1] 相加写回 SRAM 0x200
    CtVectorDesc vdesc{};
    vdesc.op = CtVecOp::add;
    vdesc.src0_fc32_idx = 0;
    vdesc.src1_fc32_idx = 0;
    vdesc.src1_from_fc32 = true;
    vdesc.dst_from_fc32 = false;
    vdesc.dst_sram_idx = 0x200;
    vdesc.wait_en = true;
    vdesc.wait_sync_id = 0;
    vdesc.wait_val = 1;
    vdesc.sig_en = false;
    vdesc.float_num = 2;

    // 矩阵引擎还没跑，0 号计数器为 0 -> 向量引擎被门控拦住返回 false
    EXPECT_FALSE(vec.callVector(vdesc));

    // 3. 运行矩阵引擎：结果存入 FC32[0..1]，并给 0 号计数器 signal(+1)
    CtMatmulDesc mdesc{0x00, 0x100, 0, 1, 2, 2, true, 0};
    EXPECT_TRUE(mat.matMul(mdesc));
    EXPECT_FLOAT_EQ(fc32.read(0), 11.0f);
    EXPECT_FLOAT_EQ(fc32.read(1), 16.0f);

    // 4. 再次运行向量引擎：此时 0 号计数器已达标，顺利算出 [22.0, 32.0] 并写回 SRAM 0x200
    EXPECT_TRUE(vec.callVector(vdesc));

    std::vector<float> out(2, 0.0f);
    CtAccess rd_out{0x200, reinterpret_cast<uint8_t *>(out.data()), false,
                    CtInitiator::LocalEngine, 8};
    ASSERT_EQ(win.access(rd_out, 0).status, CtStatus::Ok);
    EXPECT_FLOAT_EQ(out[0], 22.0f);
    EXPECT_FLOAT_EQ(out[1], 32.0f);
}
