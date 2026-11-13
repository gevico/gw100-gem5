/**
 * @file ct_nidma.cc
 * @brief compute tile 内部的 nidma：数据请求，noc 数据处理
 *
 * @see spec-ct
 */

#include<functional>
#include <algorithm>
#include <cstring>

#include "src/gw100/ct/ct_if.hh"
#include "src/gw100/ct/ct_types.hh"
#include "src/gw100/ct/nidma/ct_nidma.hh"

namespace gem5
{
namespace gw100
{

CtNidma::CtNidma(CtClockIf &clock_if,CtNocIf &noc_if, CtWindowHandler &win_handler)
    : clock_if(clock_if),noc_if(noc_if),win_handler(win_handler),buf{}
{}

CtAccessResult
CtNidma::recvExternalPacket(CtAccess &a,Tick now){
    CtAccessResult res;
    if (a.size > NocPacketLimit){
        res.status = CtStatus::Reject;
        return res;
    }
    res = win_handler.access(a, now);
    return res; 
}

/** 核心入口 */
bool
CtNidma::subDma(const CtNidmaDesc &desc, std::function<void(CtStatus)> done){
    if (busy()){
        return false;
    }else{
        /** dma开始，全部初始化 */
        isBusy = true;
        sendBytes = 0;
        recvBytes = 0;
        flightBytes = 0;
        hasError = false;
        curDesc = desc;
        invoke = done;
        buf.assign(desc.len,0);
    }

    if(!desc.is_read){
        /** 写入外部单元，先取数据 */
        if (!loadWindows()) {
             isBusy = false; 
             return false; 
        }
    }
    packetSlice();
    return true;
}

void
CtNidma::step(){
    if(isBusy && noc_if.canSend()){
        packetSlice();
    }
}

void
CtNidma::reset(){
    recvBytes = 0;
    flightBytes = 0;
    sendBytes = 0;
    hasError = false;
    isBusy = false;
    //todo vector empty
    invoke = nullptr;
    curDesc = CtNidmaDesc{};
    buf.clear();
}

bool
CtNidma::busy()const{
    return isBusy;
}

void
CtNidma::packetSlice(){
    while(isBusy && sendBytes < curDesc.len){
        if (!noc_if.canSend()){
            /** noc被占用时特定周期后唤醒 */
            clock_if.wakeAt(clock_if.now() + clock_if.period());
            return;
        }
        unsigned offset = sendBytes;
        Addr curDpa = curDesc.remote_dpa + sendBytes;
        Addr rem = curDesc.len - sendBytes;
        Addr to256b = 256 - (curDpa % 256);
        Addr chunk_len = std::min({NocPacketLimit,rem,to256b});
        sendBytes += chunk_len;
        flightBytes += chunk_len;
        if (curDesc.is_read){
            //触发 read 触发回调
            noc_if.sendRead(curDpa, chunk_len,[this,offset,chunk_len](CtStatus status, const uint8_t* data) {
                /** 单次包的计算 */
                flightBytes -= chunk_len;
                recvBytes += chunk_len;
                if(status != CtStatus::Ok || data == nullptr){
                    hasError = true;
                }else{
                    std::memcpy(&buf[offset],data,chunk_len);
                }
                /** 最终单次 read 请求处理 */
                if (recvBytes == curDesc.len){
                    if(!hasError){
                        if(!FlushWindows()){
                            hasError = true;
                        }
                    }
                    isBusy = false;
                        if (invoke != nullptr){
                            /** 通知控制核 */
                            invoke(hasError?CtStatus::Fault : CtStatus::Ok);
                    }
                }
            });
        }else{
            /** 向外部写数据，交给 noc 网络处理 */
            noc_if.sendWrite(curDpa, chunk_len, &buf[offset],
                 [this, chunk_len](CtStatus status) {
                     flightBytes -= chunk_len;
                     recvBytes += chunk_len;
                     if (status != CtStatus::Ok) {
                         hasError = true;
                     }
                     if (recvBytes == curDesc.len) {
                         isBusy = false;
                         if (invoke != nullptr) {
                             /** 通知控制核 */
                             invoke(hasError ? CtStatus::Fault : CtStatus::Ok);
                    }
                }
            });
        }
    }
}

/** 从本地 curDesc.local_offest 读进 buf */
bool
CtNidma::loadWindows(){
    unsigned index = 0;
    while(index <curDesc.len){
        Addr curLocal = curDesc.local_offest + index;
        Addr to256b = CtSramBankLimit - (curLocal % CtSramBankLimit);
        Addr rem = curDesc.len - index;
        Addr chunk = std::min({CtSramBankLimit, rem, to256b});
        CtAccess a = CtAccess{
            .base = curLocal,
            .data = &buf[index],
            .write = false,
            .initiator = CtInitiator::LocalEngine,
            .size = (unsigned)chunk,
        };
        CtAccessResult res = win_handler.access(a, clock_if.now());
        if (res.status != CtStatus::Ok){
            return false;
        }
        index += chunk;
    }
    return true;
}

bool
CtNidma::FlushWindows(){
    unsigned index = 0;
    while(index >= 0 && index < curDesc.len){
        Addr curLocal = curDesc.local_offest + index;
        Addr to256b = CtSramBankLimit - (curLocal % CtSramBankLimit);

        Addr rem = curDesc.len - index;
        Addr chunk = std::min({CtSramBankLimit, rem, to256b});
        CtAccess a = CtAccess{
            .base = curLocal,
            .data = &buf[index],
            .write = true,
            .initiator = CtInitiator::LocalEngine,
            .size = (unsigned)chunk,
        };
        CtAccessResult res = win_handler.access(a, clock_if.now());
        if (res.status != CtStatus::Ok){
            return false;
        }
        index += chunk;
    }
    return true;
}

}
}