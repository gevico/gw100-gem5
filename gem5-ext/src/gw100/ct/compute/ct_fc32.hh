/**
 * @file ct_fc32.hh
 * @brief compute tile fp32，用于矩阵引擎和向量引擎间寄存器级别通信
 *
 * @see spec-ct
 */

#ifndef __GW100_CT_FC32_CT_FC32_HH__
#define __GW100_CT_FC32_CT_FC32_HH__

#include "src/gw100/ct/ct_if.hh"
#include "src/gw100/ct/ct_types.hh"

namespace gem5
{
namespace gw100
{
class CtFc32
{
    public:
    void write(unsigned idx, float val){
        fc32s[idx] = val;
    };
    float read(unsigned idx) const{
        return fc32s[idx];
    };
    void reset(){
        fc32s.fill(0);
    };
    static constexpr unsigned Capability = 4096;
    private:
    std::array<float,Capability> fc32s;
};
}
}

#endif
