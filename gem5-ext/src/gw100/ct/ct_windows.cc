/**
 * @file ct_windows.cc
 * @brief compute tile 内部的窗口访问构建，可考虑和外部框架放在一起
 *        单独写出的原因是为了更清晰，将外部的地址量转化成构建的内部偏移
 *        并送往对应构建
 */

#include <cstring>
#include <vector>

#include "src/gw100/ct/ct_if.hh"
#include "src/gw100/ct/ct_types.hh"
#include "src/gw100/ct/ct_windows.hh"

namespace gem5
{
namespace gw100
{

void
CtWindow::reset(){
    for (auto &sw : SubWindows){
        sw.handler -> reset();
    }
};

CtWindow::CtWindow(CtWindowHandler &sram,CtWindowHandler &sync,Addr base){
    SubWindows = {
    {CtSramBase, CtSramSize, &sram},
    {CtSyncBase, CtSyncSize, &sync},
    };
    windowsBase = base;
};

CtStatus
CtWindow::normalizeAddr(Addr base, unsigned size, Addr &right_off)const{
    if(size >= CtWindowSize ){
        return CtStatus::Reject;
    }

    if(base < CtWindowSize){
        if(size <= CtWindowSize - base){
            right_off = base;
            return CtStatus::Ok;
        }
        return CtStatus::Reject;
    }

    if(windowsBase == 0){
        Addr offset = (base - CtBase) % CtWindowSize;
        if(base >= CtBase + 64 * CtWindowSize || (offset + size)<=CtWindowSize){
            return CtStatus::Reject;
        }
        right_off = offset;
        return CtStatus::Ok;
    }else{
        if(base < windowsBase || base >windowsBase + CtWindowSize){
            return CtStatus::Reject;
        }
        Addr offset = base % CtWindowSize;
        right_off = offset;
        return CtStatus::Ok;
    }

    //todo 处理同 tile 间的访问问题，需要和 types 进行同步

    return CtStatus::Reject;
};

CtAccessResult 
CtWindow::access(CtAccess &a, Tick now){
    CtAccessResult res;

    if(a.data == nullptr || a.size <=0){
        res.status = CtStatus::Fault;
        return res;
    }

    Addr offset = 0;
    CtStatus status = normalizeAddr(a.base,a.size,offset);
    if(status != CtStatus::Ok){
        res.status = status;
        return res;
    }

    for(auto &sw : SubWindows){
        if(offset >= sw.base && (a.size + offset) <= (sw.base + sw.size)){
            CtAccess sub = a;
            sub.base = offset;
            return sw.handler->access(sub,now);
        };
        res.status = CtStatus::Reject;
        return res;
    }

};
}
}
