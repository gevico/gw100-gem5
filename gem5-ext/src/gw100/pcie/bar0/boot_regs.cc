/**
 * @file  boot_regs.cc
 * @brief BootRegs 的实现
 *
 * @see spec-pcie
 */

#include "src/gw100/pcie/bar0/boot_regs.hh"

namespace gem5
{
namespace gw100
{

namespace
{

/** 寄存器回调的参数类型 */
using Reg = RegisterBankLE::Register32;

/** 本块中已定义的偏移（spec-pcie §6.4） */
constexpr Addr Offsets[] = {
    0x4000, 0x4004, 0x4010, 0x4014, 0x4018,
};

/** 启动阶段：固件运行；FW_READY 位于 bit 8 */
constexpr uint32_t BootStatusReady = 4u | (1u << 8);

/** 启动窗口与镜像大小的上限 mgmt.fw_load_max（spec-mgmt-tile §4.1） */
constexpr uint32_t FwLoadMax = 1024 * 1024;

} // anonymous namespace

/** 构造并登记各寄存器 */
BootRegs::BootRegs()
{
    regBootStatus.reader([](Reg &) { return BootStatusReady; }).readonly();
    regBootCtrl.reader([](Reg &) -> uint32_t { return 0; })
        .writer([](Reg &, const uint32_t &) {});
    regFwLoadMax.reader([](Reg &) { return FwLoadMax; }).readonly();
    regFwSize.reader([this](Reg &) { return fwSize; })
        // FW_SIZE 只在 STAGE 2 可写；桩始终处于 STAGE 4，写入被忽略
        .writer([](Reg &, const uint32_t &) {});
    regFwVersion.reader([](Reg &) -> uint32_t { return 0; }).readonly();

    bank.addRegistersAt<RegisterBankLE::RegisterRaz>({
        {0x4000, regBootStatus}, {0x4004, regBootCtrl},
        {0x4010, regFwLoadMax}, {0x4014, regFwSize}, {0x4018, regFwVersion},
    });
}

/** BAR0 偏移 off 处的 32 位字是否是本块已实现的寄存器 */
bool
BootRegs::implemented(Addr off) const
{
    for (Addr a : Offsets) {
        if (a == off)
            return true;
    }
    return false;
}

/** 保存 FW_SIZE */
void
BootRegs::serialize(CheckpointOut &cp) const
{
    SERIALIZE_SCALAR(fwSize);
}

/** 恢复 serialize() 保存的状态 */
void
BootRegs::unserialize(CheckpointIn &cp)
{
    UNSERIALIZE_SCALAR(fwSize);
}

} // namespace gw100
} // namespace gem5
