/**
 * @file ct_matrix_engine.cc
 * @brief compute tile 的矩阵引擎，负责矩阵乘法
 */

#include <algorithm>
#include <vector>

#include "src/gw100/ct/ct_if.hh"
#include "src/gw100/ct/ct_types.hh"
#include "src/gw100/ct/sync/ct_sync.hh"
#include "src/gw100/ct/compute/ct_fc32.hh"
#include "src/gw100/ct/compute/ct_matrix_engine.hh"

namespace gem5
{
namespace gw100
{

CtMatrix::CtMatrix(CtClockIf &clock_if, CtWindowHandler &win_handler, CtSync &sync, CtFc32 &fc32)
    : clock_if(clock_if),win_handler(win_handler),sync(sync),fc32(fc32)
{
}

/** 和 nidma 中的 loadwindows 逻辑一样，后续或许抽离出来单独的工具函数 */
bool
CtMatrix::loadSram(Addr mat_base,float *dst,unsigned elem_count){
    /** 加载数据的总数据值 */
    Addr total_bytes = elem_count * sizeof(float);
    unsigned index = 0;
    while(index < total_bytes){
        Addr curLocal = mat_base + index;
        Addr to256b = CtSramBankLimit - (curLocal % CtSramBankLimit);
        Addr rem = total_bytes - index;
        Addr chunk = std::min({CtSramBankLimit,rem,to256b});
        CtAccess a = CtAccess{
            .base = curLocal,
            /** 按 chunk 进行地址便宜读出要发送数据的起点 */
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
CtMatrix::matMul(const CtMatmulDesc &desc,std::function<void(CtStatus)> done){
    /** 越界和有效性检查 */
    if(desc.K == 0 || desc.M == 0 ||  desc.N == 0 ||(desc.dst_offset + desc.M * desc.N) > CtFc32::Capability){
        if(done != nullptr){
            done(CtStatus::Fault);
        }
        return false;
    }

    std::vector<float> matA(desc.M*desc.K);
    std::vector<float> matB(desc.K*desc.N);

    bool read = loadSram(desc.a_offset,matA.data(),(desc.M * desc.K));
    if(!read){
        if(done != nullptr){
            done(CtStatus::Fault);
        }
        return false;
    }

    bool write = loadSram(desc.b_offset, matB.data(),(desc.K * desc.N));
    if(!write){
        if(done != nullptr){
            done(CtStatus::Fault);
        }
        return false;
    }

    /** 暂定以行为索引形式 */
    for(unsigned m = 0; m < desc.M ; m++){
        for(unsigned n = 0; n < desc.N; n++){
            float sum = 0.0f;
            for(unsigned k = 0; k < desc.K; k++){
                sum += matA[m*desc.K + k] * matB[k*desc.N + n];
            }
            fc32.write(desc.dst_offset + m * desc.N + n, sum);
        }
    }

    /** 硬件同步器行为 */
    if(desc.sig_en){
        if(!sync.signal(desc.sync_id)){
            if(done != nullptr){
                done(CtStatus::Fault);
            }
            return false;
        }
    }

    if(done != nullptr){
        done(CtStatus::Ok);
        return true;
    } 
    return true;
}

}
}