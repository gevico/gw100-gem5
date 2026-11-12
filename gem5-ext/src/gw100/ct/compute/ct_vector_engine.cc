/**
 * @file ct_matrix_engine.cc
 * @brief compute tile 的向量引擎
 */
#include <cmath> 
#include <algorithm>
#include <vector>

#include "src/gw100/ct/ct_if.hh"
#include "src/gw100/ct/ct_types.hh"
#include "src/gw100/ct/sync/ct_sync.hh"
#include "src/gw100/ct/compute/ct_fc32.hh"
#include "src/gw100/ct/compute/ct_vector_engine.hh"

namespace gem5
{
namespace gw100
{

CtVector::CtVector(CtClockIf &clock_if,CtWindowHandler &win_handler,CtSync &sync,CtFc32 &fc32)
     : clock_if(clock_if),win_handler(win_handler),sync(sync),fc32(fc32)
{}

bool
CtVector::loadSram(Addr sram_base,float *dst,unsigned elem_count){
    /** 加载数据的总数据值 */
    Addr total_bytes = elem_count * sizeof(float);
    unsigned index = 0;
    while(index < total_bytes){
        Addr curLocal = sram_base + index;
        Addr to256b = CtSramBankLimit - (curLocal % CtSramBankLimit);
        Addr rem = total_bytes - index;
        Addr chunk = std::min({CtSramBankLimit,rem,to256b});
        CtAccess a = CtAccess{
            .base = curLocal,
            .data = reinterpret_cast<uint8_t*>(dst)+index,
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
CtVector::writeSram(Addr sram_base,float *dst,unsigned elem_count){
        unsigned index = 0;
        Addr total_bytes = elem_count * sizeof(float);
        while(index >= 0 && index < total_bytes){
        Addr curLocal = sram_base + index;
        Addr to256b = CtSramBankLimit - (curLocal % CtSramBankLimit);

        Addr rem = total_bytes - index;
        Addr chunk = std::min({CtSramBankLimit, rem, to256b});
        CtAccess a = CtAccess{
            .base = curLocal,
            .data = reinterpret_cast<uint8_t*>(dst)+index,
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

bool
CtVector::callVector(const CtVectorDesc &desc,std::function<void(CtStatus)> done){
    /** 越界和有效性检查 */
    if(!desc.dst_from_fc32){
        if((desc.dst_sram_idx + desc.float_num * sizeof(float)) > CtSramSize){
            if(done != nullptr){
                done(CtStatus::Fault);
            }
            return false;
        }
    }

    /** 必须等矩阵引擎计算完毕后才能读取数据 */
    if (desc.wait_en) {
        if (!sync.tryWait(desc.wait_sync_id, desc.wait_val)) {
            return false;
        }
    }

    std::vector<float> src0(desc.float_num, 0.0f);
    std::vector<float> src1(desc.float_num, 0.0f);
    std::vector<float> dest(desc.float_num, 0.0f);
    /** src0读取 */
    for(unsigned i = 0; i < desc.float_num; i++){
        src0[i] = fc32.read(desc.src0_fc32_idx+i);
    }

    /** src1读取 */
    if(desc.src1_from_fc32){
        for(unsigned i = 0; i < desc.float_num; i++){
            src1[i] = fc32.read(desc.src1_fc32_idx+i);
        }
    }else{
        if(!loadSram(desc.src1_sram_idx, src1.data(),desc.float_num )){
            if(done != nullptr){
                done(CtStatus::Fault);
                return false;
            } 
        }
    }

    /** 具体 op 运算 */
    for(unsigned i = 0; i < desc.float_num ; i++){
        switch(desc.op){
            case CtVecOp::add :{
                dest[i] = src0[i] + src1[i];
                break;
            }
            case CtVecOp::mult :{
                dest[i] = src0[i] * src1[i];
                break;
            }
            case CtVecOp::silu :{
                dest[i] = src0[i] / (1.0f + std::exp(-src0[i]));
                break;
            }
        }
    }

    /** 写回远端内存 */   
    if(desc.dst_from_fc32){
        for(unsigned i = 0; i < desc.float_num; i++){
            fc32.write(desc.dst_fc32_idx+i, dest[i]);
        }
    }else{
        if(!writeSram(desc.dst_sram_idx, dest.data(), desc.float_num)){
            if(done != nullptr){
                done(CtStatus::Fault);
                return false;
            } 
        }
    }

    if(desc.sig_en){
        if(!sync.signal(desc.sig_sync_id,1)){
            if(done != nullptr){
                done(CtStatus::Fault);
            } 
            return false;
        }
    }

    if(done != nullptr){
        done(CtStatus::Ok);
    }
    return true;
    }
}


}
