/**
 * @file  mgmt_interrupts.cc
 * @brief MgmtInterrupts 的实现
 *
 * @see spec-mgmt-tile
 */

#include "src/gw100/mgmt/intr/mgmt_interrupts.hh"

#include <memory>

#include "arch/riscv/pcstate.hh"
#include "cpu/thread_context.hh"
#include "sim/faults.hh"

namespace gem5
{
namespace gw100
{

/** 停机"中断"：记下 PC，转去取 haltPc */
class MgmtInterrupts::HaltFault : public FaultBase
{
  public:
    /** 构造：intr 为发出请求的中断控制器 */
    explicit HaltFault(MgmtInterrupts &intr) : intr(intr) {}

    /** 名字 */
    FaultName name() const override { return "gw100_halt"; }

    /** 在指令边界取走：记下下一条待执行指令的地址，转去取 haltPc */
    void
    invoke(ThreadContext *tc, const StaticInstPtr &inst =
           nullStaticInstPtr) override
    {
        (void)inst;
        intr.dpc = tc->pcState().instAddr();
        intr.taken = true;
        std::unique_ptr<PCStateBase> pc(tc->pcState().clone());
        pc->set(intr.haltPc);
        tc->pcState(*pc);
    }

  private:
    /** 发出请求的中断控制器 */
    MgmtInterrupts &intr;
};

/** 构造 */
MgmtInterrupts::MgmtInterrupts(const Params &p) : RiscvISA::Interrupts(p)
{
}

/**
 * 请求 CPU 在下一个指令边界停住，停住后去取 halt_pc 处的指令；
 * 请求保持到 clearHalt()
 */
void
MgmtInterrupts::requestHalt(Addr halt_pc)
{
    haltReq = true;
    taken = false;
    haltPc = halt_pc;
}

/** 有停机请求，或者有可以取走的中断 */
bool
MgmtInterrupts::checkInterrupts() const
{
    return (haltReq && !taken) || RiscvISA::Interrupts::checkInterrupts();
}

/** 停机请求优先于全部中断 */
Fault
MgmtInterrupts::getInterrupt()
{
    if (haltReq && !taken)
        return std::make_shared<HaltFault>(*this);
    return RiscvISA::Interrupts::getInterrupt();
}

/** 取走停机请求时不更新中断信息 */
void
MgmtInterrupts::updateIntrInfo()
{
    if (!(haltReq && !taken))
        RiscvISA::Interrupts::updateIntrInfo();
}

} // namespace gw100
} // namespace gem5
