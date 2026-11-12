/**
 * @file ct_ctrl.cc
 * @brief compute tile 控制核实现
 */

#include <vector>
#include <functional>

#include "src/gw100/ct/ct_if.hh"
#include "src/gw100/ct/ct_types.hh"
#include "src/gw100/ct/nidma/ct_nidma.hh"
#include "src/gw100/ct/compute/ct_vector_engine.hh"
#include "src/gw100/ct/compute/ct_matrix_engine.hh"
#include "src/gw100/ct/ctrl/ct_ctrl.hh"

namespace gem5
{
namespace gw100
{

CtCtrl::CtCtrl(CtClockIf &clock_if,CtNidma &nidma,CtMatrix &matrix,CtVector &vector)
    :clock_if(clock_if),nidma(nidma),matrix(matrix),vector(vector), cmds{}
{}

void
CtCtrl::reset(){
    pc = 0;
    cmds.clear();
    status = CtCtrlStatus::Idle;
}

void
CtCtrl::start(){
    if(status != CtCtrlStatus::Idle){
        return;
    }
    status = CtCtrlStatus::Running;
    pc = 0;
    step();
    return;
}

void
CtCtrl::loadParams(const std::vector<CtCmd> &programs){
    pc = 0;
    status = CtCtrlStatus::Idle;
    cmds = programs;
    return;
}

CtCtrlStatus
CtCtrl::getStatus()const{
    return status;
}

void
CtCtrl::step(){
    /** 判断状态 */
    if (status != CtCtrlStatus::Running) {
        return;
    }
    if (nidma.busy()) {
        clock_if.wakeAt(clock_if.now() + clock_if.period());
        return;
    }

    /** pc状态判断 */
    if(pc >= cmds.size() || cmds[pc].Op == CtCtrlOp::End){
        status = CtCtrlStatus::Done;
        return;
    }

   const CtCmd &inst = cmds[pc];
    switch(inst.Op){
        case CtCtrlOp::Matmul :{
            if(!matrix.matMul(inst.Matmul,[this](CtStatus st){
                if(st != CtStatus::Ok){
                    status = CtCtrlStatus::Fault;
                }
            })){
                return;
            }
            pc++;
            break;
        }
        case CtCtrlOp::Vector :{
            if(!vector.callVector(inst.Vector,[this](CtStatus st){
                if(st != CtStatus::Ok){
                    status = CtCtrlStatus::Fault;
                }
            })){
                if(status == CtCtrlStatus::Running){
                    clock_if.wakeAt(clock_if.now() + clock_if.period());
                }
                return;
            }
            pc++;
            break;
        }
        case CtCtrlOp::Nidma :{
            if(!nidma.subDma(inst.Nidma,[this](CtStatus st){
                if(st != CtStatus::Ok){
                    status = CtCtrlStatus::Fault;
                } 
            })){
                return;
            }
            pc++;
            break;
        }
    }

    /** 下一步维系 */
    if(status == CtCtrlStatus::Running){
        clock_if.wakeAt(clock_if.now()+clock_if.period());
    }
    return;
}
}
}