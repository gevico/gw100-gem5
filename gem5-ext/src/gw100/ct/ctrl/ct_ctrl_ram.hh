/**
 * @file ct_ctrl_ram.hh
 * @brief compute tile 控制核的 ram 单元
 */
#ifndef __GW100__CT_CTRL_RAM_CT_CTRL_RAM_HH__
#define __GW100__CT_CTRL_RAM_CT_CTRL_RAM_HH__

#include <vector>
#include <cstdint>
#include <functional>

#include "src/gw100/ct/ct_if.hh"
#include "src/gw100/ct/ct_types.hh"

namespace gem5
{
namespace gw100
{
class CtCtrlSram : public CtWindowHandler
{
    public:
    explicit CtCtrlSram(CtClockIf &clock_if); 
    /** 对外总线接口 */
    CtAccessResult access(CtAccess &a, Tick now) override;
    void reset() override;
    /** 对 ctrl 接口 */
    /** 寄存器读写接口 */
    uint32_t readReg(Addr offset) const;
    void writeReg(Addr offset, uint32_t val);
    /** 写入 private 的回调函数 */
    void setOnStart(std::function<void()> cb) { onStart = std::move(cb); }
    /** cmd 读写接口 */
    void writeCmd(unsigned idx, const CtCmd &cmd);
    CtCmd readCmd(unsigned idx) const;

    private:
    CtClockIf &clock_if;
    std::vector<uint8_t> storage;
    /** 记录下一次空闲的时间,需要每次进行更新 */
    Tick freeTick = 0;
    /** 当往 sram 中写第一位的时候启动 start 函数 */
    std::function<void()> onStart;

};
}
}
#endif