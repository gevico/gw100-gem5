/**
 * @file ct_sram.cc
 * @brief CtSram 的实现
 * 
 * @see spec-ct
 */

#include "src/gw100/ct/sram/ct_sram.hh"

#include <algorithm>

#include "base/logging.hh"
#include "src/gw100/ct/ct_types.hh"

namespace gem5
{
namespace gw100
{

//todo 添加当前 now 时钟的获取

CtSram::CtSram(CtClockIf &clock_if)
    : clockIf(clock_if), bankFree{}, storage(CtSramSize, 0)
{
}

/** 当前 sram 下的内部偏移 */
Addr
CtSram::normalizeAddr(Addr addr)
{
    Addr offset = (addr >= CtBase) ? (addr - CtBase) % CtWindowSize : addr;
    return offset - CtSramBase;
}

/** offset 11:8 为上层编号前 8 位是 bank 内字节偏移 */
Addr
CtSram::bankOf(Addr offset)
{
    return (offset >> BankShift) & (Numbank - 1);
}

void
CtSram::reset()
{
    bankFree.fill(0);
}

CtAccessResult
CtSram::access(CtAccess &a, Tick now)
{
    CtAccessResult res;

    /** access 出现非法字段 fault*/
    if (a.size == 0 || a.data == nullptr) {
        res.status = CtStatus::Fault;
        return res;
    }

    Addr offset = normalizeAddr(a.base);

    /** 容量超过 sram 限制，reject */
    if ((offset + a.size) > CtSramSize) {
        res.status = CtStatus::Reject;
        return res;
    }

    /** 单次访问超过 256B 或者 首尾不在单 256B 内，reject */
    if (a.size > CtSramBankLimit ||
        (offset / CtSramBankLimit) !=
            ((offset + a.size - 1) / CtSramBankLimit)) {
        res.status = CtStatus::Reject;
        return res;
    }

    /** 数据读写，通过 access 的 data 字段进行访问 */
    if (a.write) {
        std::memcpy(&storage[offset], a.data, a.size);
    } else {
        std::memcpy(a.data, &storage[offset], a.size);
    }

    /** 计算实际耗时 */
    Tick cost = schedule(offset, a.size, a.write, now, clockIf.period());

    res.status = CtStatus::Ok;
    res.delay = cost - now;
    return res;
}

Tick
CtSram::schedule(Addr a, unsigned size, bool write, Tick now, Tick period)
{
    /** 计算所设计的具体word */
    Addr first = a / WordSize;
    Addr last = (a + size - 1) / WordSize;
    unsigned accesses = last - first + 1;


    /** 如果是写入，字非覆盖情况下的写入需要先将原来的 64 位取出将剩余部分保存，因此需要额外判断 */
    if (write) {
        /** 检验到底是不是写入字数非 64 位对齐 */
        bool first_partial = (a % WordSize != 0) ||
                             (first == last && (a + size) % WordSize != 0);
        bool last_partial = (last != first) && ((a + size) % WordSize != 0);
        accesses += (first_partial ? 1 : 0) + (last_partial ? 1 : 0);
    }

    unsigned b = bankOf(a);
    Tick start = std::max(now, bankFree[b]);
    bankFree[b] = start + accesses * period;
    return bankFree[b];
}

}
}
