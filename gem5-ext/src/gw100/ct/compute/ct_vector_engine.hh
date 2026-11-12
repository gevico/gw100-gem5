/**
 * @file ct_matrix_engine.hh
 * @brief compute tile 的向量引擎
 */
#ifndef __GW100_CT_VECTOR_CT_VECTOR_HH__
#define __GW100_CT_VECTOR_CT_VECTOR_HH__

#include "src/gw100/ct/ct_if.hh"
#include "src/gw100/ct/ct_types.hh"
#include "src/gw100/ct/sync/ct_sync.hh"
#include "src/gw100/ct/compute/ct_fc32.hh"

namespace gem5
{
namespace gw100
{
class CtVector
{
    public:
    explicit CtVector(CtClockIf &clock_if,CtWindowHandler &win_handler,CtSync &sync,CtFc32 &fc32);
    /** 向量乘法 */
    bool callVector(const CtVectorDesc &desc,std::function<void(CtStatus)> done = nullptr);

    private:
    /** 辅助函数，读取 sram 中的矩阵 */
    bool loadSram(Addr sram_base,float *dst,unsigned elem_count);
    /** 辅助函数，写入 sram 中的矩阵 */
    bool writeSram(Addr sram_base,float *dst,unsigned elem_count);
    CtClockIf &clock_if;
    CtFc32 &fc32;
    CtWindowHandler &win_handler;
    CtSync &sync;
};
}
}

#endif