/**
 * @file  mgmt_crc.hh
 * @brief 时钟复位控制器（CRC）的寄存器：复位原因、Mesh 复位、PLL、时钟选择
 *
 * 位于本地地址 0x2002_0000（spec-clock-reset-power §6），在 clk_ao 域，
 * 管理 CPU 经请求/应答握手访问，一次访问需要若干个 XTAL 周期。
 * - PLL：写 EN 后经过锁定时间置位 LOCK；PLL_TS 复位后由硬件自动使能。
 * - CLK_SEL：只能把 clk_mgmt、clk_core 由 XTAL 切到已锁定的 PLL，切换由
 *   无毛刺的选择器完成，期间时钟暂停（§3.4）。clk_mgmt 的切换改变管理
 *   系统时钟域的频率档；写 CLK_SEL 的响应在切换完成后才返回，相当于
 *   管理 CPU 在暂停期间停住。
 * - Mesh 与 Compute Tile 的复位、时钟使能、NI 隔离寄存器按规格保存，
 *   对 NoC 和 Tile 的作用随它们的模型实现。
 *
 * @see spec-clock-reset-power
 */

#ifndef __GW100_MGMT_CRC_MGMT_CRC_HH__
#define __GW100_MGMT_CRC_MGMT_CRC_HH__

#include <cstdint>
#include <functional>

#include "base/types.hh"

namespace gem5
{
namespace gw100
{

/** 时钟复位控制器的寄存器 */
class MgmtCrc
{
  public:
    /** 本地地址：CRC 寄存器的起始 */
    static constexpr Addr Base = 0x2002'0000;
    /** CRC 寄存器区的大小 */
    static constexpr Addr Size = 0x1000;

    /** CRC 的时序参数 */
    struct Config
    {
        /** PLL 的锁定时间（clock.pll_lock_timeout 以内） */
        Tick pllLock = 0;
        /** 一次寄存器访问的握手延迟 */
        Tick access = 0;
        /** 无毛刺时钟选择器切换时，时钟暂停的时间 */
        Tick switchPause = 0;
    };

    /** 构造；now 为复位撤销的时刻，PLL_TS 从此时开始锁定 */
    MgmtCrc(const Config &cfg, Tick now);

    /** 读偏移 off 的 32 位寄存器；未实现的偏移返回 false */
    bool read(Addr off, uint32_t &v, Tick now) const;

    /**
     * 写偏移 off 的 32 位寄存器，返回完成时刻；未实现的偏移返回
     * MaxTick。写 CLK_SEL 引起切换时，完成时刻在切换结束之后
     */
    Tick write(Addr off, uint32_t v, Tick now);

    /** 访问的握手延迟 */
    Tick accessLatency() const { return cfg.access; }

    /**
     * clk_mgmt 切换到 PLL_CORE 时调用：参数为切换完成的时刻。由管理
     * Tile 在那一刻改变管理系统时钟域的频率档
     */
    std::function<void(Tick)> onMgmtSwitch;

  private:
    /** 时序参数 */
    const Config cfg;
    /** CRC_RESET_CAUSE：冷复位后为 POR */
    uint32_t resetCause = 1;
    /** CRC_MESH_RST：[0] NOC，[4:1] MEM */
    uint32_t meshRst = 0x1F;
    /** CRC_CT_RST */
    uint32_t ctRst = 0xFFFF;
    /** CRC_CT_CLK_EN */
    uint32_t ctClkEn = 0;
    /** CRC_CT_ISO */
    uint32_t ctIso = 0xFFFF;
    /** CRC_PLL_CORE_CFG */
    uint32_t pllCoreCfg = 0;
    /** CRC_PLL_DDR_CFG */
    uint32_t pllDdrCfg = 0;
    /** PLL_CORE 锁定的时刻；未使能时为 MaxTick */
    Tick coreLockAt = MaxTick;
    /** PLL_DDR 锁定的时刻 */
    Tick ddrLockAt = MaxTick;
    /** PLL_TS 锁定的时刻 */
    Tick tsLockAt = MaxTick;
    /** CRC_CLK_SEL 中已经完成的选择：[0] CORE，[1] MGMT */
    uint32_t clkSel = 0;
    /** CLK_SEL 中尚未完成的切换完成的时刻 */
    Tick switchDoneAt = 0;
    /** 切换完成后 CLK_SEL 的值 */
    uint32_t clkSelNext = 0;
};

} // namespace gw100
} // namespace gem5

#endif // __GW100_MGMT_CRC_MGMT_CRC_HH__
