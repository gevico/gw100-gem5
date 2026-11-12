/**
 * @file ct_ctrl_ram.hh
 * @brief compute tile 控制核的 ram 单元
 */

#include <vector>
#include <algorithm>
#include <cstring>

#include "src/gw100/ct/ct_if.hh"
#include "src/gw100/ct/ct_types.hh"
#include "src/gw100/ct/ctrl/ct_ctrl_ram.hh"

namespace gem5
{
namespace gw100
{
CtCtrlSram::CtCtrlSram(CtClockIf &clock_if)
    :clock_if(clock_if),storage(CtrlRamSize, 0)
{}

void
CtCtrlSram::reset(){
    freeTick = 0;
    std::fill(storage.begin(),storage.end(),0);
}

CtAccessResult
CtCtrlSram::access(CtAccess &a,Tick now){
    CtAccessResult res;

    /** access 出现非法字段 fault*/
    if (a.size == 0 || a.data == nullptr) {
        res.status = CtStatus::Fault;
        return res;
    }

    Addr win_off = a.base % CtWindowSize;
    Addr offset = (win_off >= CtrlRamBase) ? (win_off - CtrlRamBase) : win_off;

    if((offset + a.size) > CtrlRamSize){
        res.status = CtStatus::Reject;
        return res;
    }

    if (a.write) {
        std::memcpy(&storage[offset], a.data, a.size);
        /** 判断是否写入第零位 */
        if(offset == 0 &&  onStart != nullptr){
            onStart();
        }
    } else {
        std::memcpy(a.data, &storage[offset], a.size);
    }

    /** 更新 freetick */
    Tick start = std::max(now,freeTick);
    freeTick = start + clock_if.period();
    res.status = CtStatus::Ok;
    res.delay = freeTick - now;
    return res;
}

uint32_t
CtCtrlSram::readReg(Addr offset)const{
    /** 通过 memcpy 实现连续地址单元的拷贝，起点在 data 中，范围通过具体的数据结构来限制 */
    uint32_t reg = 0;
    std::memcpy(&reg,&storage[offset],sizeof(reg));
    return reg;
}

void
CtCtrlSram::writeReg(Addr offset, uint32_t val)
{
    std::memcpy(&storage[offset], &val, sizeof(val));
}


CtCmd
CtCtrlSram::readCmd(unsigned idx)const{
    CtCmd cmd;
    std::memcpy(&cmd,&storage[CtrlSramCmd + idx * sizeof(CtCmd)],sizeof(CtCmd));
    return cmd;
}

void
CtCtrlSram::writeCmd(unsigned idx, const CtCmd &cmd){
    std::memcpy(&storage[CtrlSramCmd + idx * sizeof(CtCmd)],&cmd,sizeof(cmd));
    return;
}
}
}