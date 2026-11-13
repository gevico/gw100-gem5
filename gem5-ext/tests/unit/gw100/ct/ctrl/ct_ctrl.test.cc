/**
 * @file  ct_ctrl.test.cc
 * @brief 单 Compute Tile 总体端到端全链路测试（CtCtrl + CtCtrlSram + CtNidma + CtMatrix + CtFc32 + CtVector + CtSync + CtWindow + CtSram）
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <vector>

#include "src/gw100/ct/compute/ct_fc32.hh"
#include "src/gw100/ct/compute/ct_matrix_engine.hh"
#include "src/gw100/ct/compute/ct_vector_engine.hh"
#include "src/gw100/ct/ct_windows.hh"
#include "src/gw100/ct/ctrl/ct_ctrl.hh"
#include "src/gw100/ct/ctrl/ct_ctrl_ram.hh"
#include "src/gw100/ct/nidma/ct_nidma.hh"
#include "src/gw100/ct/sram/ct_sram.hh"
#include "src/gw100/ct/sync/ct_sync.hh"

using namespace gem5;
using namespace gem5::gw100;

namespace
{

class SimClock : public CtClockIf
{
  public:
    Tick curTick = 1000;
    Tick clkPeriod = 1000;
    bool wakeRequested = false;

    Tick now() const override { return curTick; }
    Tick period() const override { return clkPeriod; }
    void wakeAt(Tick when) override
    {
        curTick = when;
        wakeRequested = true;
    }
};

class LoopbackNoc : public CtNocIf
{
  public:
    std::vector<uint8_t> mem;
    LoopbackNoc() : mem(8192, 0) {}

    bool canSend() const override { return true; }

    void sendRead(Addr dpa, unsigned len, CtNocReadDone done) override
    {
        done(CtStatus::Ok, &mem[dpa]);
    }

    void sendWrite(Addr dpa, unsigned len, const uint8_t *data,
                   CtNocWriteDone done) override
    {
        std::memcpy(&mem[dpa], data, len);
        done(CtStatus::Ok);
    }
};

} // anonymous namespace

/**
 * 总体全链路测试：
 * 1. Cmd 0 (Nidma) : 从外部内存 0x00 搬入 A(1x2) 和 B(2x2) 到本地 CtSram
 * 2. Cmd 1 (Matmul): 算 A * B 存入 CtFc32[0..1]，并给 CtSync[0] 加 1
 * 3. Cmd 2 (Vector): 等 CtSync[0] >= 1，算 FC32[0..1] * FC32[0..1] 写回 CtSram 0x400
 * 4. Cmd 3 (Nidma) : 把 CtSram 0x400 的结果写回外部内存 0x1000
 * 5. Cmd 4 (End)   : 结束并置 CtrlStatusOff = Done
 */
TEST(CtPipelineTest, EndToEndSingleTileExecution)
{
    SimClock clk;
    LoopbackNoc noc;
    CtSram sram(clk);
    CtSync sync(clk);
    CtCtrlSram ctrl_ram(clk);
    CtWindow win(sram, sync, ctrl_ram);
    CtFc32 fc32;
    fc32.reset();

    CtNidma nidma(clk, noc, win);
    CtMatrix mat(clk, win, sync, fc32);
    CtVector vec(clk, win, sync, fc32);
    CtCtrl ctrl(clk, nidma, mat, vec, ctrl_ram);

    // 在外部内存准备好 A = [2.0, 3.0] (偏移 0x00)，B = [[1.0, 2.0], [3.0, 4.0]] (偏移 0x08)
    std::vector<float> init_data = {2.0f, 3.0f, 1.0f, 2.0f, 3.0f, 4.0f};
    std::memcpy(noc.mem.data(), init_data.data(), init_data.size() * sizeof(float));

    std::vector<CtCmd> prog(5);
    // Cmd 0: Nidma 读入 24 字节到本地 SRAM 0x00
    prog[0].Op = CtCtrlOp::Nidma;
    prog[0].Nidma = CtNidmaDesc{0x00, 0x00, 24, true};

    // Cmd 1: Matmul 算 A(偏移 0) * B(偏移 8) -> FC32[0..1] (结果为 [11.0, 16.0])，触发 sync 0
    prog[1].Op = CtCtrlOp::Matmul;
    prog[1].Matmul = CtMatmulDesc{0x00, 0x08, 0, 1, 2, 2, true, 0};

    // Cmd 2: Vector 等 sync 0，算 FC32[0..1] * FC32[0..1] = [121.0, 256.0] 写回 SRAM 0x400
    prog[2].Op = CtCtrlOp::Vector;
    prog[2].Vector.op = CtVecOp::mult;
    prog[2].Vector.src0_fc32_idx = 0;
    prog[2].Vector.src1_fc32_idx = 0;
    prog[2].Vector.src1_from_fc32 = true;
    prog[2].Vector.dst_from_fc32 = false;
    prog[2].Vector.dst_sram_idx = 0x400;
    prog[2].Vector.wait_en = true;
    prog[2].Vector.wait_sync_id = 0;
    prog[2].Vector.wait_val = 1;
    prog[2].Vector.sig_en = false;
    prog[2].Vector.float_num = 2;

    // Cmd 3: Nidma 把 SRAM 0x400 的 8 字节结果写回外部内存 0x1000
    prog[3].Op = CtCtrlOp::Nidma;
    prog[3].Nidma = CtNidmaDesc{0x1000, 0x400, 8, false};

    // Cmd 4: End
    prog[4].Op = CtCtrlOp::End;

    // 装载指令到 CtCtrlSram，并通过 CtWindow 往 CtrlRamBase + 0 写 1 点火启动！
    ctrl.loadParams(prog);
    uint32_t start_flag = 1;
    CtAccess start_acc{CtrlRamBase, reinterpret_cast<uint8_t *>(&start_flag),
                       true, CtInitiator::MT, 4};
    ASSERT_EQ(win.access(start_acc, clk.now()).status, CtStatus::Ok);

    // 沿着 wakeAt 推进控制核直到完成
    for (int guard = 0; guard < 20 && ctrl.getStatus() == CtCtrlStatus::Running; ++guard) {
        ctrl.step();
    }

    // 验证状态已变为 Done，且 CtCtrlSram 内部状态寄存器同步为 Done
    EXPECT_EQ(ctrl.getStatus(), CtCtrlStatus::Done);
    EXPECT_EQ(ctrl_ram.readReg(CtrlStatusOff),
              static_cast<uint32_t>(CtCtrlStatus::Done));

    // 验证外部内存 0x1000 处收到了最终计算结果 [121.0, 256.0]
    float res0 = 0.0f, res1 = 0.0f;
    std::memcpy(&res0, &noc.mem[0x1000], sizeof(float));
    std::memcpy(&res1, &noc.mem[0x1004], sizeof(float));
    EXPECT_FLOAT_EQ(res0, 121.0f);
    EXPECT_FLOAT_EQ(res1, 256.0f);
}
