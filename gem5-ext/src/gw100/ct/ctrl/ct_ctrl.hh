/**
 * @file ct_ctrl.hh
 * @brief compute tile 的控制核
 */

#ifndef __GW100__CT_CTRL_CT_CTRL_HH__
#define __GW100__CT_CTRL_CT_CTRL_HH__

#include <vector>

#include "src/gw100/ct/nidma/ct_nidma.hh"
#include "src/gw100/ct/compute/ct_vector_engine.hh"
#include "src/gw100/ct/compute/ct_matrix_engine.hh"
#include "src/gw100/ct/ctrl/ct_ctrl_ram.hh"
#include "src/gw100/ct/ct_if.hh"
#include "src/gw100/ct/ct_types.hh"

namespace gem5
{
namespace gw100
{
class CtCtrl
{
    public:
    explicit CtCtrl(CtClockIf &clock_if,CtNidma &nidma,CtMatrix &matrix,CtVector &vector,CtCtrlSram &ctrl_sram);
    /** 沿时钟调用，主要的处理函数 */
    void step();
    void reset();
    void start();

    /** 装载指令归零 pc */
    void loadParams(const std::vector<CtCmd> &programs);
    CtCtrlStatus getStatus() const;
    private:
    CtNidma &nidma;
    CtVector &vector;
    CtMatrix &matrix;
    CtClockIf &clock_if;
    CtCtrlSram &ctrl_sram;

    /** 指令储存和 pc */
    unsigned pc = 0;
    CtCtrlStatus status = CtCtrlStatus::Idle;
};
}
}

#endif