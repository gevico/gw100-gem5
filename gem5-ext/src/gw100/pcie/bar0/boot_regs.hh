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

    /** 构造并登记各寄存器 */
    BootRegs();

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

    /** 把 FW_SIZE 恢复为复位值 */
    void reset() override { fwSize = 0; }

    /** 保存 FW_SIZE */
    void serialize(CheckpointOut &cp) const override;

    /** 恢复 serialize() 保存的状态 */
    void unserialize(CheckpointIn &cp) override;

  private:
    /** FW_SIZE */
    uint32_t fwSize = 0;

    /** 本块的寄存器组 */
    RegisterBankLE bank{"boot", Base};
    /** BOOT_STATUS（0x4000，RO） */
    RegisterBankLE::Register32 regBootStatus{"BOOT_STATUS"};
    /** BOOT_CTRL（0x4004，W1S，本阶段写入无效果） */
    RegisterBankLE::Register32 regBootCtrl{"BOOT_CTRL"};
    /** FW_LOAD_MAX（0x4010，RO） */
    RegisterBankLE::Register32 regFwLoadMax{"FW_LOAD_MAX"};
    /** FW_SIZE（0x4014，RW；只在 STAGE 2 可写，桩中写入被忽略） */
    RegisterBankLE::Register32 regFwSize{"FW_SIZE"};
    /** FW_VERSION（0x4018，RO） */
    RegisterBankLE::Register32 regFwVersion{"FW_VERSION"};
};

} // namespace gw100
} // namespace gem5

#endif // __GW100_PCIE_BAR0_BOOT_REGS_HH__
