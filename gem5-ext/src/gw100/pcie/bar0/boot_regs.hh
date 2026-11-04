/**
 * @file  boot_regs.hh
 * @brief BAR0 的启动与固件寄存器块（0x4000）
 *
 * 暂定的桩：上电即处于 STAGE 4 且 FW_READY；启动窗口、固件加载与校验
 * 不建模（理想化清单 #11）。
 *
 * @see spec-pcie
 */

#ifndef __GW100_PCIE_BAR0_BOOT_REGS_HH__
#define __GW100_PCIE_BAR0_BOOT_REGS_HH__

#include <cstdint>

#include "base/types.hh"
#include "dev/reg_bank.hh"
#include "src/gw100/pcie/boot/boot_ctrl.hh"
#include "src/gw100/pcie/pcie_types.hh"

namespace gem5
{
namespace gw100
{

/** 启动与固件寄存器（spec-pcie §6.4） */
class BootRegs : public RegBlock
{
  public:
    /** 本块在 BAR0 中的起始偏移 */
    static constexpr Addr Base = 0x4000;

    /** 构造并登记各寄存器；ctrl 提供启动状态，fw_load_max 为装载区大小 */
    BootRegs(BootCtrl &ctrl, uint32_t fw_load_max);

    /** 本块在 BAR0 中的起始偏移 */
    Addr base() const override { return Base; }

    /** BAR0 偏移 off 处的 32 位字是否是本块已实现的寄存器 */
    bool implemented(Addr off) const override;

    /** 读 BAR0 偏移 off 起的 size 字节到 data；调用者已检查宽度和偏移 */
    void read(Addr off, uint8_t *data, unsigned size) override
    { bank.read(off, data, size); }

    /** 把 data 中的 size 字节写到 BAR0 偏移 off 起；调用者已检查 */
    void write(Addr off, const uint8_t *data, unsigned size) override
    { bank.write(off, data, size); }

    /**
     * FLR 时由 BootCtrl::flr() 处理 FW_SIZE、FW_READY 等；FW_VERSION 由
     * 固件维护，FLR 不清零。这里没有需要复位的状态
     */
    void reset() override {}

    /** FW_VERSION 的当前值 */
    uint32_t fwVersion() const { return fwVer; }

    /** 固件经 FW_VERSION_FW 写入 FW_VERSION（spec-pcie §6.5） */
    void setFwVersion(uint32_t v) { fwVer = v; }

    /** 设备完全复位：FW_VERSION 清零（FLR 保留它） */
    void devReset() { fwVer = 0; }

    /** 保存 FW_VERSION */
    void serialize(CheckpointOut &cp) const override;

    /** 恢复 serialize() 保存的状态 */
    void unserialize(CheckpointIn &cp) override;

  private:
    /** 启动状态 */
    BootCtrl &ctrl;
    /** 装载区大小（FW_LOAD_MAX） */
    const uint32_t fwLoadMax;
    /** FW_VERSION */
    uint32_t fwVer = 0;

    /** 本块的寄存器组 */
    RegisterBankLE bank{"boot", Base};
    /** BOOT_STATUS（0x4000，RO） */
    RegisterBankLE::Register32 regBootStatus{"BOOT_STATUS"};
    /** BOOT_CTRL（0x4004，W1S）：[0] FW_START，[1] DEV_RESET */
    RegisterBankLE::Register32 regBootCtrl{"BOOT_CTRL"};
    /** FW_LOAD_MAX（0x4010，RO） */
    RegisterBankLE::Register32 regFwLoadMax{"FW_LOAD_MAX"};
    /** FW_SIZE（0x4014，RW；只在启动窗口打开时可写） */
    RegisterBankLE::Register32 regFwSize{"FW_SIZE"};
    /** FW_VERSION（0x4018，RO） */
    RegisterBankLE::Register32 regFwVersion{"FW_VERSION"};
};

} // namespace gw100
} // namespace gem5

#endif // __GW100_PCIE_BAR0_BOOT_REGS_HH__
