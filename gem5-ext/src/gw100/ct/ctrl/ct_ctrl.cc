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
#include "src/gw100/ct/ctrl/ct_ctrl_ram.hh"
#include "src/gw100/ct/ctrl/ct_ctrl.hh"

namespace gem5
{
namespace gw100
{

CtCtrl::CtCtrl(CtClockIf &clock_if,CtNidma &nidma,CtMatrix &matrix,CtVector &vector,CtCtrlSram &ctrl_sram)
    :clock_if(clock_if),nidma(nidma),matrix(matrix),vector(vector),ctrl_sram(ctrl_sram)
{
    ctrl_sram.setOnStart([this] { start(); });
}

void
CtCtrl::reset(){
    pc = 0;
    status = CtCtrlStatus::Idle;
}

void
CtCtrl::start(){
    if(status != CtCtrlStatus::Idle){
        return;
    }
    status = CtCtrlStatus::Running;
    ctrl_sram.writeReg(CtrlStatusOff, static_cast<uint32_t>(status));
    pc = 0;
    step();
    return;
}

void
CtCtrl::loadParams(const std::vector<CtCmd> &programs){
    pc = 0;
    status = CtCtrlStatus::Idle;
    ctrl_sram.writeReg(CtrlStatusOff, static_cast<uint32_t>(status));
    /** 将 programs 写入  ctrl sram 的 cmd 段中 */
    ctrl_sram.writeReg(CtrlCmdNumOff, static_cast<uint32_t>(programs.size()));
    for (unsigned i = 0; i < programs.size(); ++i) {
        ctrl_sram.writeCmd(i, programs[i]);
    }
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
    uint32_t cmd_num = ctrl_sram.readReg(CtrlCmdNumOff);
    if (pc >= cmd_num) {
        status = CtCtrlStatus::Done;
        ctrl_sram.writeReg(CtrlStatusOff, static_cast<uint32_t>(status));
        return;
    }
    const CtCmd inst = ctrl_sram.readCmd(pc);
    /** 判断是否到 cmd 结束，单独先判断 */
    if (inst.Op == CtCtrlOp::End) {
        status = CtCtrlStatus::Done;
        ctrl_sram.writeReg(CtrlStatusOff, static_cast<uint32_t>(status));
        return;
    }

    switch(inst.Op){
        case CtCtrlOp::Matmul :{
            if(!matrix.matMul(inst.Matmul,[this](CtStatus st){
                if(st != CtStatus::Ok){
                    status = CtCtrlStatus::Fault;
                    ctrl_sram.writeReg(CtrlStatusOff, static_cast<uint32_t>(status));
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
                    ctrl_sram.writeReg(CtrlStatusOff, static_cast<uint32_t>(status));
                }
            })){
                /** 等待 sync 而非数据错误,目前这种写法依赖于在 vector 中非等待 sync 退出前调用 fault 的隐式约定 */
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
                    ctrl_sram.writeReg(CtrlStatusOff, static_cast<uint32_t>(status));
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