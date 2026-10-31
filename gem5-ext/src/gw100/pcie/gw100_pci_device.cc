/**
 * @file  gw100_pci_device.cc
 * @brief Gw100PciDevice 的实现
 *
 * @see spec-pcie
 */

#include "src/gw100/pcie/gw100_pci_device.hh"

#include <cstring>

#include "base/bitfield.hh"
#include "base/trace.hh"
#include "debug/Gw100Pcie.hh"
#include "dev/pci/pcireg.h"
#include "mem/packet_access.hh"
#include "sim/core.hh"
#include "sim/cur_tick.hh"
#include "sim/serialize.hh"
#include "sim/system.hh"

namespace gem5
{
namespace gw100
{

namespace
{

/** 寄存器回调的参数类型 */
using Reg = RegisterBankLE::Register32;

/** 全局寄存器中已定义的偏移（spec-pcie §6.1） */
constexpr Addr GlobalRegs[] = {
    0x000, 0x004, 0x008, 0x00C, 0x010, 0x014, 0x018,
    0x020, 0x024, 0x028, 0x030, 0x034, 0x038,
};

/** 启动寄存器中已定义的偏移（spec-pcie §6.4） */
constexpr Addr BootRegs[] = {
    0x4000, 0x4004, 0x4010, 0x4014, 0x4018,
};

/** 启动阶段：固件运行；FW_READY 位于 bit 8 */
constexpr uint32_t BootStatusReady = 4u | (1u << 8);

/** DEV_STATUS 的 DEBUG_WIN 位：固件已开启 BAR2 调试窗口 */
constexpr uint32_t DevStatusDebugWin = 1u << 17;

/** 允许 host 写入的最大固件大小（暂定） */
constexpr uint32_t FwLoadMax = 16 * 1024 * 1024;

/** off 是否在 list 中 */
template <size_t N>
bool
listed(const Addr (&list)[N], Addr off)
{
    for (Addr a : list) {
        if (a == off)
            return true;
    }
    return false;
}

/** 以小端序把 16 位值写到 a[off] 起的两个字节 */
void
put16(std::array<uint8_t, 0x1000> &a, Addr off, uint16_t v)
{
    a[off] = v & 0xFF;
    a[off + 1] = v >> 8;
}

/** 以小端序把 32 位值写到 a[off] 起的四个字节 */
void
put32(std::array<uint8_t, 0x1000> &a, Addr off, uint32_t v)
{
    for (int i = 0; i < 4; i++)
        a[off + i] = (v >> (8 * i)) & 0xFF;
}

} // anonymous namespace

/**
 * 写 BAR 寄存器：交给 PciMemBar 处理后在写回值中置 bit 3
 * （Prefetchable）；bit 3 是硬件固定位，不参与地址计算
 */
uint32_t
Gw100PrefetchMemBar::write(const PciUpstream::DeviceInterface &interface,
                           uint32_t val)
{
    return PciMemBar::write(interface, val & ~0x8u) | 0x8u;
}

/** 按参数初始化寄存器视图、全局与启动寄存器组和 capability */
Gw100PciDevice::Gw100PciDevice(const Params &p)
    : PciEndpoint(p),
      version(p.version),
      devCaps((p.bar2_window_count & 0xFF) |
              (uint32_t(p.msix_table_size) << 8) |
              (uint32_t(p.hdi_max_contexts) << 16) |
              (uint32_t(p.hdma_channels) << 24)),
      winCount(p.bar2_window_count),
      winSize(p.bar2_window_size),
      bar2Latency(p.bar2_latency),
      devmemBase(p.devmem_base),
      devmemSize(p.devmem_size),
      devicePaBits(p.device_pa_bits),
      timestampHz(p.timestamp_hz),
      linkSpeed(p.link_speed),
      linkWidth(p.link_width),
      memPort(name() + ".mem_port"),
      memRequestorId(p.system->getRequestorId(this, "devmem")),
      windows(p.bar2_window_count)
{
    fatal_if(!isPowerOf2(winSize), "%s: BAR2 窗口大小必须是 2 的幂", name());
    fatal_if(winCount == 0 || winCount > 0xFF, "%s: BAR2 窗口个数越界",
             name());
    fatal_if(!BARs[2] || BARs[2]->size() != winCount * winSize,
             "%s: BAR2 大小必须等于窗口个数 × 窗口大小", name());
    fatal_if(devicePaBits < 32 || devicePaBits > 64,
             "%s: 设备物理地址宽度越界", name());

    regDevVersion.reader([this](Reg &) { return version; }).readonly();
    regDevCaps.reader([this](Reg &) { return devCaps; }).readonly();
    regDevStatus.reader([this](Reg &) -> uint32_t {
        // 桩模型相当于固件已在运行并开启了调试窗口，BAR2 窗口始终有效
        return (linkSpeed & 0xF) | (uint32_t(linkWidth & 0x1F) << 4) |
               DevStatusDebugWin;
    }).readonly();
    regScratch.reader([this](Reg &) { return scratch; })
        .writer([this](Reg &, const uint32_t &v) { scratch = v; });
    regTsLo.reader([this](Reg &) -> uint32_t {
        uint64_t t = timestamp();
        tsLatchedHi = uint32_t(t >> 32);
        return uint32_t(t);
    }).readonly();
    regTsHi.reader([this](Reg &) { return tsLatchedHi; }).readonly();
    regTsFreq.reader([this](Reg &) { return timestampHz; }).readonly();
    regIntStatus.reader([this](Reg &) { return intStatus; })
        .writer([this](Reg &, const uint32_t &v) {
            intStatus &= ~v;
            updateIntStatus();
        });
    regIntMask.reader([this](Reg &) { return intMask; })
        .writer([this](Reg &, const uint32_t &v) { intMask = v; });
    regIntSet.reader([](Reg &) -> uint32_t { return 0; })
        .writer([this](Reg &, const uint32_t &v) { intStatus |= v; });
    regErrStatus.reader([this](Reg &) { return errStatus; })
        .writer([this](Reg &, const uint32_t &v) {
            errStatus &= ~v;
            updateIntStatus();
        });
    regErrAddr.reader([this](Reg &) { return errAddr; }).readonly();
    regErrInfo.reader([this](Reg &) { return errInfo; }).readonly();

    globalBank.addRegistersAt<RegisterBankLE::RegisterRaz>({
        {0x000, regDevVersion}, {0x004, regDevCaps}, {0x008, regDevStatus},
        {0x00C, regScratch}, {0x010, regTsLo}, {0x014, regTsHi},
        {0x018, regTsFreq}, {0x020, regIntStatus}, {0x024, regIntMask},
        {0x028, regIntSet}, {0x030, regErrStatus}, {0x034, regErrAddr},
        {0x038, regErrInfo},
    });

    regBootStatus.reader([](Reg &) { return BootStatusReady; }).readonly();
    regBootCtrl.reader([](Reg &) -> uint32_t { return 0; })
        .writer([](Reg &, const uint32_t &) {});
    regFwLoadMax.reader([](Reg &) { return FwLoadMax; }).readonly();
    regFwSize.reader([this](Reg &) { return fwSize; })
        // FW_SIZE 只在 STAGE 2 可写；桩始终处于 STAGE 4，写入被忽略
        .writer([](Reg &, const uint32_t &) {});
    regFwVersion.reader([](Reg &) -> uint32_t { return 0; }).readonly();

    bootBank.addRegistersAt<RegisterBankLE::RegisterRaz>({
        {0x4000, regBootStatus}, {0x4004, regBootCtrl},
        {0x4010, regFwLoadMax}, {0x4014, regFwSize}, {0x4018, regFwVersion},
    });

    buildCapabilities();
}

/**
 * 返回名为 if_name 的端口：mem_port 是设备内存端口，其余交给
 * PciEndpoint
 */
Port &
Gw100PciDevice::getPort(const std::string &if_name, PortID idx)
{
    if (if_name == "mem_port")
        return memPort;
    return PciEndpoint::getPort(if_name, idx);
}

/**
 * 构造 PM 和 PCI Express capability 的初值、可写位和写 1 清零位，
 * 并保存为复位值
 */
void
Gw100PciDevice::buildCapabilities()
{
    // PCI Power Management（spec-pcie §4.2）：版本 3，只支持 D0/D3hot，
    // No_Soft_Reset = 0
    cfg[PmCap] = 0x01;
    cfg[PmCap + 1] = PcieCap;
    put16(cfg, PmCap + 2, 0x0003);
    put16(cfg, PmCsr, 0x0000);
    cfgWmask[PmCsr] = 0x03;

    // PCI Express Capability，版本 2，Endpoint；本阶段是链表的最后一项
    cfg[PcieCap] = 0x10;
    cfg[PcieCap + 1] = 0x00;
    put16(cfg, PcieCap + 0x02, 0x0002);
    // Device Capabilities：MPS Supported = 256 B，Extended Tag，FLR Capable
    put32(cfg, PcieCap + 0x04, 0x1u | (1u << 5) | (1u << 28));
    // Device Control：Enable Relaxed Ordering、Extended Tag Enable、
    // Enable No Snoop 默认为 1，MPS = 128 B，MRRS = 512 B
    put16(cfg, PcieDevCtl, (1u << 4) | (1u << 8) | (1u << 11) | (2u << 12));
    put16(cfgWmask, PcieDevCtl, 0x79FF);
    // Device Status：错误检测位写 1 清零
    put16(cfgW1c, PcieCap + 0x0A, 0x000F);
    // Link Capabilities：最高 Gen4（16 GT/s）、x4、只支持 ASPM L1
    put32(cfg, PcieCap + 0x0C, 0x4u | (4u << 4) | (2u << 10) | (6u << 15));
    // Link Control：ASPM 控制、Common Clock、Extended Synch 可写
    put16(cfgWmask, PcieCap + 0x10, 0x00C3);
    // Link Status：当前速率与宽度，Slot Clock Configuration
    put16(cfg, PcieCap + 0x12,
          (linkSpeed & 0xF) | ((linkWidth & 0x3F) << 4) | (1u << 12));
    // Device Capabilities 2：完成超时范围 A、B，不支持关闭完成超时
    put32(cfg, PcieCap + 0x24, 0x3);
    // Device Control 2：完成超时值可写
    put16(cfgWmask, PcieCap + 0x28, 0x000F);
    // Link Capabilities 2：支持 2.5/5/8/16 GT/s
    put32(cfg, PcieCap + 0x2C, 0x1E);
    // Link Control 2：Target Link Speed 默认 Gen4，可写
    put16(cfg, PcieCap + 0x30, 0x0004);
    put16(cfgWmask, PcieCap + 0x30, 0x000F);

    // 扩展配置空间 0x100：头为 0，表示没有扩展 capability
    cfgReset = cfg;
}

/** 把 BAR0 寄存器状态和全部 BAR2 窗口恢复为复位值 */
void
Gw100PciDevice::resetRegisters()
{
    scratch = 0;
    intStatus = 0;
    intMask = 0xFFFFFFFF;
    errStatus = 0;
    errAddr = 0;
    errInfo = 0;
    tsLatchedHi = 0;
    fwSize = 0;
    for (auto &w : windows)
        w = Window{};
}

/**
 * 构造一次配置写交给 PciEndpoint，用于复位配置空间头部（Command、
 * BAR），使 BAR 对象的状态一并更新
 */
void
Gw100PciDevice::cfgWriteCommon(Addr off, unsigned size, uint32_t val)
{
    Addr addr = upstreamInterface.configRange().start() + off;
    RequestPtr req = std::make_shared<Request>(addr, size, 0, memRequestorId);
    Packet pkt(req, MemCmd::WriteReq);
    uint8_t buf[4] = {};
    pkt.dataStatic(buf);
    if (size == 2)
        pkt.setLE<uint16_t>(uint16_t(val));
    else
        pkt.setLE<uint32_t>(val);
    PciEndpoint::writeConfig(&pkt);
}

/**
 * 功能级复位（FLR、D3hot→D0）：恢复 capability 和寄存器，清零
 * Command 和全部 BAR；设备内存内容保留
 */
void
Gw100PciDevice::functionReset(const char *why)
{
    DPRINTF(Gw100Pcie, "%s：复位配置空间、BAR 和寄存器\n", why);
    cfg = cfgReset;
    resetRegisters();
    cfgWriteCommon(PCI_COMMAND, 2, 0);
    for (int i = 0; i < 6; i++)
        cfgWriteCommon(PCI0_BASE_ADDR0 + 4 * i, 4, 0);
}

/** 当前设备时间戳：仿真时间换算到 timestamp_hz */
uint64_t
Gw100PciDevice::timestamp() const
{
    unsigned __int128 t = curTick();
    return uint64_t(t * timestampHz / sim_clock::Frequency);
}

/** 错误条件仍然成立时置 INT_STATUS.ERROR */
void
Gw100PciDevice::updateIntStatus()
{
    // 错误条件仍然成立时，ERROR 位清零后立即重新置位（spec-pcie §8.4）
    if (errStatus)
        intStatus |= IntError;
}

/**
 * 置 ERR_STATUS 的 bit；ERR_STATUS 原为 0 时锁存这次错误的偏移和
 * 访问信息
 */
void
Gw100PciDevice::logError(uint32_t bit, Addr off, bool is_write,
                         unsigned size, unsigned bar)
{
    // 只记录 ERR_STATUS 从全 0 变为非 0 时的那一次（spec-pcie §6.3）
    if (errStatus == 0) {
        errAddr = uint32_t(off);
        errInfo = (is_write ? 1u : 0u) | ((size & 0xF) << 2) |
                  ((bar & 0x3) << 6);
    }
    errStatus |= bit;
    updateIntStatus();
    DPRINTF(Gw100Pcie, "错误 %#x：BAR%u 偏移 %#x，%s %u 字节\n", bit, bar,
            off, is_write ? "写" : "读", size);
}

/** [off, off + size) 覆盖的每个 32 位字是否都是已实现的寄存器 */
bool
Gw100PciDevice::bar0Implemented(Addr off, unsigned size) const
{
    for (Addr w = off & ~Addr(3); w < off + size; w += 4) {
        Addr block = w & ~(Bar0BlockSize - 1);
        bool ok;
        if (block == GlobalBlock)
            ok = listed(GlobalRegs, w);
        else if (block == WindowBlock)
            ok = (w - WindowBlock) < winCount * WindowStride;
        else if (block == BootBlock)
            ok = listed(BootRegs, w);
        else
            ok = false;
        if (!ok)
            return false;
    }
    return true;
}

/**
 * BAR0 访问：先按 spec-pcie §5.2 检查宽度和偏移，出错时记录并以
 * 成功完成结束；合法访问分派到寄存器组或窗口配置
 */
Tick
Gw100PciDevice::accessBar0(PacketPtr pkt, Addr off, bool is_write)
{
    const unsigned size = pkt->getSize();
    const bool aligned = (off % size) == 0;
    const bool width_ok = is_write ? (size == 4 || size == 8) && aligned
                                   : (size == 1 || size == 2 || size == 4 ||
                                      size == 8) && aligned;

    if (!width_ok || !bar0Implemented(off, size)) {
        logError(width_ok ? ErrRsvdAccess : ErrBadWidth, off, is_write, size,
                 0);
        if (!is_write)
            std::memset(pkt->getPtr<uint8_t>(), 0, size);
        pkt->makeAtomicResponse();
        return pioDelay;
    }

    const Addr block = off & ~(Bar0BlockSize - 1);
    if (block == GlobalBlock || block == BootBlock) {
        RegisterBankLE &bank = block == GlobalBlock ? globalBank : bootBank;
        if (is_write)
            bank.write(off, pkt->getConstPtr<void>(), size);
        else
            bank.read(off, pkt->getPtr<void>(), size);
    } else {
        // BAR2 窗口配置：按 32 位字逐个处理
        const uint32_t base_lo_mask = uint32_t(~(winSize - 1));
        const uint32_t base_hi_mask =
            devicePaBits >= 64 ? 0xFFFFFFFFu
                               : uint32_t(mask(devicePaBits - 32));
        uint8_t *data = pkt->getPtr<uint8_t>();
        for (Addr w = off & ~Addr(3); w < off + size; w += 4) {
            Window &win = windows[(w - WindowBlock) / WindowStride];
            const Addr field = (w - WindowBlock) % WindowStride;
            uint32_t val = 0;
            if (is_write) {
                std::memcpy(&val, data + (w - off), 4);
                val = letoh(val);
                if (field == 0x0)
                    win.ctrl = val & (WinCtrlEn | WinCtrlRdOnly);
                else if (field == 0x8)
                    win.base = (win.base & ~mask(32)) | (val & base_lo_mask);
                else if (field == 0xC)
                    win.base = (win.base & mask(32)) |
                               (uint64_t(val & base_hi_mask) << 32);
            } else {
                if (field == 0x0)
                    val = win.ctrl;
                else if (field == 0x8)
                    val = uint32_t(win.base);
                else if (field == 0xC)
                    val = uint32_t(win.base >> 32);
                val = htole(val);
                for (unsigned i = 0; i < 4; i++) {
                    Addr a = w + i;
                    if (a >= off && a < off + size)
                        data[a - off] = reinterpret_cast<uint8_t *>(&val)[i];
                }
            }
        }
    }

    pkt->makeAtomicResponse();
    return pioDelay;
}

/**
 * BAR2 访问：按窗口换算成设备物理地址，检查使能、目标范围和只读后
 * 经 mem_port 功能访问设备内存
 */
Tick
Gw100PciDevice::accessBar2(PacketPtr pkt, Addr off, bool is_write)
{
    const unsigned size = pkt->getSize();
    const Window &win = windows[off / winSize];
    const Addr target = win.base + off % winSize;
    uint32_t err = 0;

    if (!(win.ctrl & WinCtrlEn))
        err = ErrWinDisabled;
    else if (target < devmemBase || target + size > devmemBase + devmemSize)
        err = ErrWinTarget;
    else if (is_write && (win.ctrl & WinCtrlRdOnly))
        err = ErrWinTarget;

    if (err) {
        logError(err, off, is_write, size, 2);
        if (!is_write)
            std::memset(pkt->getPtr<uint8_t>(), 0xFF, size);
    } else {
        RequestPtr req =
            std::make_shared<Request>(target, size, 0, memRequestorId);
        Packet fpkt(req, is_write ? MemCmd::WriteReq : MemCmd::ReadReq);
        fpkt.dataStatic(pkt->getPtr<uint8_t>());
        memPort.sendFunctional(&fpkt);
    }

    pkt->makeAtomicResponse();
    return bar2Latency;
}

/** BAR 读访问的入口：按 BAR 号分派到 accessBar0() 或 accessBar2() */
Tick
Gw100PciDevice::readDevice(PacketPtr pkt)
{
    int bar;
    Addr off;
    panic_if(!getBAR(pkt->getAddr(), bar, off),
             "%s: 访问地址 %#x 不在任何 BAR 内", name(), pkt->getAddr());
    return bar == 0 ? accessBar0(pkt, off, false)
                    : accessBar2(pkt, off, false);
}

/** BAR 写访问的入口：按 BAR 号分派到 accessBar0() 或 accessBar2() */
Tick
Gw100PciDevice::writeDevice(PacketPtr pkt)
{
    int bar;
    Addr off;
    panic_if(!getBAR(pkt->getAddr(), bar, off),
             "%s: 访问地址 %#x 不在任何 BAR 内", name(), pkt->getAddr());
    return bar == 0 ? accessBar0(pkt, off, true)
                    : accessBar2(pkt, off, true);
}

/** 配置读：0x40 以下交给基类，0x40 起由本类的 capability 提供 */
Tick
Gw100PciDevice::readConfig(PacketPtr pkt)
{
    const Addr off = pkt->getAddr() & (CfgSize - 1);
    if (off < CfgOwnBase)
        return PciDevice::readConfig(pkt);

    const unsigned size = pkt->getSize();
    panic_if(off + size > CfgSize, "%s: 配置空间访问越界", name());
    uint32_t val = 0;
    for (unsigned i = 0; i < size; i++)
        val |= uint32_t(cfg[off + i]) << (8 * i);
    switch (size) {
      case 1: pkt->setLE<uint8_t>(uint8_t(val)); break;
      case 2: pkt->setLE<uint16_t>(uint16_t(val)); break;
      case 4: pkt->setLE<uint32_t>(val); break;
      default: panic("%s: 非法的配置空间访问宽度 %u", name(), size);
    }
    pkt->makeAtomicResponse();
    return configDelay;
}

/**
 * 配置写：0x40 以下交给基类；0x40 起按可写位和写 1 清零位更新，
 * 并处理 FLR 与 D3hot→D0
 */
Tick
Gw100PciDevice::writeConfig(PacketPtr pkt)
{
    const Addr off = pkt->getAddr() & (CfgSize - 1);
    if (off < CfgOwnBase)
        return PciEndpoint::writeConfig(pkt);

    const unsigned size = pkt->getSize();
    panic_if(off + size > CfgSize, "%s: 配置空间访问越界", name());
    uint32_t val;
    switch (size) {
      case 1: val = pkt->getLE<uint8_t>(); break;
      case 2: val = pkt->getLE<uint16_t>(); break;
      case 4: val = pkt->getLE<uint32_t>(); break;
      default: panic("%s: 非法的配置空间访问宽度 %u", name(), size);
    }

    const uint8_t old_pm = cfg[PmCsr] & 0x3;
    for (unsigned i = 0; i < size; i++) {
        const Addr b = off + i;
        const uint8_t byte = uint8_t(val >> (8 * i));
        cfg[b] = (cfg[b] & ~cfgWmask[b]) | (byte & cfgWmask[b]);
        cfg[b] &= ~(byte & cfgW1c[b]);
    }
    // 不支持 D1/D2：请求进入这两个状态时忽略写入
    const uint8_t new_pm = cfg[PmCsr] & 0x3;
    if (new_pm == 1 || new_pm == 2)
        cfg[PmCsr] = (cfg[PmCsr] & ~0x3) | old_pm;

    // Initiate FLR 位于 Device Control 的 bit 15
    const Addr flr_byte = PcieDevCtl + 1;
    const bool flr = off <= flr_byte && flr_byte < off + size &&
                     ((val >> (8 * (flr_byte - off))) & 0x80);

    pkt->makeAtomicResponse();
    if (flr)
        functionReset("FLR");
    else if (old_pm == 3 && (cfg[PmCsr] & 0x3) == 0)
        functionReset("D3hot→D0");  // No_Soft_Reset = 0
    return configDelay;
}

/**
 * 保存寄存器状态、BAR2 窗口和配置空间 0x40 以上的内容；配置空间
 * 头部由基类保存
 */
void
Gw100PciDevice::serialize(CheckpointOut &cp) const
{
    PciEndpoint::serialize(cp);
    SERIALIZE_SCALAR(scratch);
    SERIALIZE_SCALAR(intStatus);
    SERIALIZE_SCALAR(intMask);
    SERIALIZE_SCALAR(errStatus);
    SERIALIZE_SCALAR(errAddr);
    SERIALIZE_SCALAR(errInfo);
    SERIALIZE_SCALAR(tsLatchedHi);
    SERIALIZE_SCALAR(fwSize);
    std::vector<uint32_t> win_ctrl;
    std::vector<uint64_t> win_base;
    for (const auto &w : windows) {
        win_ctrl.push_back(w.ctrl);
        win_base.push_back(w.base);
    }
    SERIALIZE_CONTAINER(win_ctrl);
    SERIALIZE_CONTAINER(win_base);
    SERIALIZE_ARRAY(cfg.data(), cfg.size());
}

/** 从 checkpoint 恢复 serialize() 保存的状态 */
void
Gw100PciDevice::unserialize(CheckpointIn &cp)
{
    PciEndpoint::unserialize(cp);
    UNSERIALIZE_SCALAR(scratch);
    UNSERIALIZE_SCALAR(intStatus);
    UNSERIALIZE_SCALAR(intMask);
    UNSERIALIZE_SCALAR(errStatus);
    UNSERIALIZE_SCALAR(errAddr);
    UNSERIALIZE_SCALAR(errInfo);
    UNSERIALIZE_SCALAR(tsLatchedHi);
    UNSERIALIZE_SCALAR(fwSize);
    std::vector<uint32_t> win_ctrl;
    std::vector<uint64_t> win_base;
    UNSERIALIZE_CONTAINER(win_ctrl);
    UNSERIALIZE_CONTAINER(win_base);
    fatal_if(win_ctrl.size() != windows.size(), "%s: checkpoint 窗口数不符",
             name());
    for (size_t i = 0; i < windows.size(); i++)
        windows[i] = Window{win_ctrl[i], win_base[i]};
    UNSERIALIZE_ARRAY(cfg.data(), cfg.size());
}

} // namespace gw100
} // namespace gem5
