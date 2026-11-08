/**
 * @file ct_sram.hh
 * @brief compute tile 内部的 sram：权重储存，计算结果放置
 * 
 *
 * @see spec-ct
 */

 #ifndef __GW100__COMPUTE_RAM_HH__
 #define __GW100__COMPUTE_RAM_HH__

#include<cstdint>
#include<vector>
#include<array>
#include<vector>
#include<algorithm>
#include<cstring>

#include "../ct_if.hh"
#include "../ct_types.hh"

namespace gem5
{
namespace gw100
{
class CtSram : public CtWindowHandler
{
    public:
    /** 全局 sram 分成 16bank 物理上连续软件上交替 */
    static constexpr unsigned Numbank = 16;
    /** 每次访问 256b 2 的 8 次方是 256 */
    static constexpr unsigned BankShift = 8;
    /** 单个字的大小 */
    static Addr WordSize = 8;
    /**
     * sram 排队通过 bankfree 记录
     */
     Tick schedule(Addr a, unsigned size, bool write,Tick now,
                   Tick period);

    /** override 上级对象 virtual 契约 */
    ~CtSram() override = default;

    /** 核心访问机制 */
    CtAccessResult access(CtAccess &a,Tick now) override;

    void reset() override;
     private:
     /** 记录 bank 使用情况，进行当下 tick 的判断*/
     std::array<Tick, Numbank> bankFree;
     /** 2mib 实际存储空间，用于 */
     std::vector<uint8_t>storage;
     /** 本地地址 a 所在的 bank */
     static constexpr Addr bankof(Addr a);
     /** 得到当前偏移 */
     static constexpr Addr normalizeAddr(Addr addr);
};
}
}

 #endif