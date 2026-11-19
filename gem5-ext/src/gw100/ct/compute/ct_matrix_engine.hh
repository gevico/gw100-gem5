/**
 * @file ct_matrix_engine.hh
 * @brief compute tile 的矩阵引擎，负责矩阵乘法
 */
#ifndef __GW100_CT_MATRIX_CT_MATRIX_HH__
#define __GW100_CT_MATRIX_CT_MATRIX_HH__

#include "src/gw100/ct/ct_if.hh"
#include "src/gw100/ct/ct_types.hh"
#include "src/gw100/ct/sync/ct_sync.hh"
#include "src/gw100/ct/compute/ct_fc32.hh"

namespace gem5
{
namespace gw100
{
class CtMatrix
{
    public:
    explicit CtMatrix(CtClockIf &clock_if, CtWindowHandler &win_handler, CtSync &sync, CtFc32 &fc32);
    /** 矩阵乘法函数 */
    bool matMul(const CtMatmulDesc &desc,std::function<void(CtStatus)> done = nullptr);

    private:
    /** 辅助函数，搬运 sram 中的矩阵 */
    bool loadSram(Addr sram_base,float *dst,unsigned elem_count);
    CtClockIf &clock_if;
    CtFc32 &fc32;
    CtWindowHandler &win_handler;
    CtSync &sync;
};
}   
}
#endif