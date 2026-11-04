/**
 * @file  mgmt_interrupts.hh
 * @brief 管理 CPU 的中断控制器：RISC-V 中断控制器加上停机请求
 *
 * gem5 的 TimingSimpleCPU 不能在任意时刻停住（有访问在途时挂起线程会
 * 触发断言），但每次取指之前都会经中断控制器检查中断，那正是指令边界。
 * 停机请求因此做成一个优先于全部中断的"中断"：CPU 在下一个指令边界
 * 取走它，记下当时的 PC，然后转去取 haltPc 处的指令；管理 Tile 扣下
 * 这次取指，CPU 就停在那里，不再执行任何指令（spec-mgmt-tile §7.4）。
 * 设备完全复位时由管理 Tile 复位 CPU 的状态，再用复位向量处的指令
 * 响应这次取指，CPU 从复位向量重新开始。
 *
 * haltPc 只是模型内部的约定（启动 ROM 末尾保留的一个字），不是架构
 * 状态；记下的 PC 相当于调试模块中的 dpc。
 *
 * @see spec-mgmt-tile
 */

#ifndef __GW100_MGMT_INTR_MGMT_INTERRUPTS_HH__
#define __GW100_MGMT_INTR_MGMT_INTERRUPTS_HH__

#include "arch/riscv/interrupts.hh"
#include "params/Gw100MgmtInterrupts.hh"

namespace gem5
{
namespace gw100
{

/** 管理 CPU 的中断控制器 */
class MgmtInterrupts : public RiscvISA::Interrupts
{
  public:
    PARAMS(Gw100MgmtInterrupts);

    /** 构造 */
    explicit MgmtInterrupts(const Params &p);

    /**
     * 请求 CPU 在下一个指令边界停住，停住后去取 halt_pc 处的指令；
     * 请求保持到 clearHalt()
     */
    void requestHalt(Addr halt_pc);

    /** 撤销停机请求（设备完全复位释放时） */
    void clearHalt() { haltReq = false; }

    /** CPU 是否已经在指令边界取走了停机请求 */
    bool haltTaken() const { return taken; }

    /** CPU 停住时下一条待执行指令的地址（相当于 dpc） */
    Addr haltedPc() const { return dpc; }

    /** 有停机请求，或者有可以取走的中断 */
    bool checkInterrupts() const override;

    /** 停机请求优先于全部中断 */
    Fault getInterrupt() override;

    /** 取走停机请求时不更新中断信息 */
    void updateIntrInfo() override;

  private:
    /** 停机"中断"：记下 PC，转去取 haltPc */
    class HaltFault;

    /** 是否有停机请求 */
    bool haltReq = false;
    /** CPU 已经取走停机请求 */
    bool taken = false;
    /** 停住后去取指的地址 */
    Addr haltPc = 0;
    /** CPU 停住时下一条待执行指令的地址 */
    Addr dpc = 0;
};

} // namespace gw100
} // namespace gem5

#endif // __GW100_MGMT_INTR_MGMT_INTERRUPTS_HH__
