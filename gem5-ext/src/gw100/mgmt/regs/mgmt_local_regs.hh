/**
 * @file  mgmt_local_regs.hh
 * @brief 管理 Tile 本地寄存器：ML、SRAM 控制、访问控制
 *
 * 位于本地地址 0x2000_D000 – 0x2000_FFFF（spec-mgmt-tile §3.5）；同一个
 * 64 KiB 区的 0x0000 – 0xBFFF 是 CLINT，由单独的设备实现，0xC000 起的
 * 看门狗是 MgmtWdt。32 位寄存器
 * 只接受 32 位访问，64 位寄存器只接受 64 位访问。
 *
 * @see spec-mgmt-tile, spec-mgmt-link
 */

#ifndef __GW100_MGMT_REGS_MGMT_LOCAL_REGS_HH__
#define __GW100_MGMT_REGS_MGMT_LOCAL_REGS_HH__

#include <array>
#include <cstdint>
#include <functional>

#include "base/types.hh"
#include "src/gw100/mgmt/sram/mgmt_sram.hh"
#include "src/gw100/ml/ml_endpoint.hh"

namespace gem5
{
namespace gw100
{

/** 管理 Tile 的本地寄存器 */
class MgmtLocalRegs
{
  public:
    /** 本地地址：本对象负责的范围起始（ML 寄存器） */
    static constexpr Addr Base = 0x2000'D000;
    /** 本对象负责的范围大小 */
    static constexpr Addr Size = 0x3000;
    /** NoC 目标端口的窗口数（mgmt.noc_windows） */
    static constexpr unsigned NocWindows = 4;
    /** ACL_VIOL_STATUS 的位：启动写入越出装载区 */
    static constexpr unsigned ViolBoot = 0;
    /** ACL_VIOL_STATUS 的位：NoC 目标端口的访问被拒绝 */
    static constexpr unsigned ViolNoc = 1;

    /** 构造：sram 提供 INIT_DONE */
    explicit MgmtLocalRegs(MgmtSram &sram);

    /** 接入 ML 管理侧适配器，ML 寄存器经它读写 */
    void bindMl(MlEndpoint *ep) { ml = ep; }

    /**
     * CPU 读本地地址 a 处 size 字节；未实现的寄存器或宽度不符时返回
     * false（访问错误异常）
     */
    bool read(Addr a, unsigned size, uint64_t &v, Tick now) const;

    /** CPU 写本地地址 a 处 size 字节；规则同 read() */
    bool write(Addr a, unsigned size, uint64_t v);

    /** HDMA 经 ML 访问 [a, a + len) 是否落在 DESC_BASE – DESC_LIMIT 内 */
    bool descAllowed(Addr a, unsigned len) const;

    /** 记录一次访问控制违例（bit 为 ViolBoot 或 ViolNoc） */
    void recordViol(unsigned bit, Addr a);

    /** PLIC 源 3 的电平：ML_ERR 非零 */
    bool mlIrq() const;

    /** PLIC 源 4 的电平：THRESH ≠ 0 且 COUNT ≥ THRESH */
    bool ceIrq() const;

    /** PLIC 源 5 的电平：SRAM_ECC_STATUS.UE */
    bool ueIrq() const { return eccStatus & 2; }

    /** PLIC 源 6 的电平：ACL_VIOL_STATUS ≠ 0 */
    bool aclIrq() const { return aclViol != 0; }

    /** 中断电平可能变化时调用 */
    std::function<void()> onChange;

  private:
    /** 一个 NoC 目标端口窗口 */
    struct NocWin
    {
        /** NOC_WIN_BASE（含） */
        uint64_t base = 0;
        /** NOC_WIN_LIMIT（不含） */
        uint64_t limit = 0;
        /** NOC_WIN_ATTR：[0] EN，[1] WR */
        uint32_t attr = 0;
    };

    /** 本地寄存器依赖的 SRAM */
    MgmtSram &sram;
    /** ML 管理侧适配器 */
    MlEndpoint *ml = nullptr;

    /** SRAM_ECC_STATUS：[0] CE，[1] UE */
    uint32_t eccStatus = 0;
    /** SRAM_ECC_CE_COUNT */
    uint32_t ceCount = 0;
    /** SRAM_ECC_CE_THRESH */
    uint32_t ceThresh = 0;
    /** SRAM_ECC_UE_ADDR */
    uint64_t ueAddr = 0;

    /** ACL_LOCK：[0] LOCK */
    bool aclLock = false;
    /** DESC_BASE（含） */
    uint64_t descBase = 0;
    /** DESC_LIMIT（不含） */
    uint64_t descLimit = 0;
    /** NoC 目标端口的窗口 */
    std::array<NocWin, NocWindows> nocWin{};
    /** ACL_VIOL_STATUS */
    uint32_t aclViol = 0;
    /** ACL_VIOL_ADDR */
    uint64_t aclViolAddr = 0;
};

} // namespace gw100
} // namespace gem5

#endif // __GW100_MGMT_REGS_MGMT_LOCAL_REGS_HH__
