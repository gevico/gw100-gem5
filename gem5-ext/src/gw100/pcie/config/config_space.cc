/**
 * @file  config_space.cc
 * @brief ConfigSpace 的实现
 *
 * @see spec-pcie
 */

#include "src/gw100/pcie/config/config_space.hh"

#include <algorithm>
#include <utility>
#include "src/gw100/pcie/link/link_model.hh"

namespace gem5
{
namespace gw100
{

/**
 * 在 base 处注册一个 capability；链表按注册顺序连接。必须在
 * finalize() 之前调用
 */
void
ConfigSpace::addCapability(Addr base, std::unique_ptr<Capability> cap)
{
    caps.push_back(Entry{base, std::move(cap)});
}

/** 填写各 capability 的 ID、next 指针和初值，并保存为复位值 */
void
ConfigSpace::finalize()
{
    for (size_t i = 0; i < caps.size(); i++) {
        const Addr base = caps[i].base;
        cfg[base] = caps[i].cap->id();
        // 最后一项的 next 为 0，表示链表结束
        cfg[base + 1] = i + 1 < caps.size() ? uint8_t(caps[i + 1].base) : 0;
        caps[i].cap->init(cfg, cfgWmask, cfgW1c, base);
    }
    if (linkExtensions) {
        setLive(0x100, 4, 0x14010001); // AER，version 1。
        setLive(0x108, 4, 1u << 22);
        setLive(0x10C, 4,
                (1u << 4) | (1u << 5) | (1u << 13) | (1u << 17) | (1u << 18) |
                    (1u << 22));
        setLive(0x114, 4, 1u << 13);
        setLive(0x140, 4, 0x18010019); // Secondary PCI Express。
        setLive(0x180, 4, 0x1A010025); // Data Link Feature。
        setLive(0x184, 4, 0x80000001);
        setLive(0x1A0, 4, 0x00010026); // Physical Layer 16 GT/s。
        auto mask = [](CfgBytes &bytes, Addr off, uint32_t value) {
            for (unsigned i = 0; i < 4; ++i) {
                bytes[off + i] = value >> (8 * i);
            }
        };
        constexpr uint32_t uncorrectable =
            (1u << 4) | (1u << 5) | (1u << 12) | (1u << 13) | (1u << 14) |
            (1u << 15) | (1u << 16) | (1u << 17) | (1u << 18) | (1u << 20) |
            (1u << 22);
        constexpr uint32_t correctable =
            1u | (1u << 6) | (1u << 7) | (1u << 8) | (1u << 12) | (1u << 13);
        mask(cfgW1c, 0x104, uncorrectable);
        mask(cfgW1c, 0x110, correctable);
        mask(cfgWmask, 0x108, uncorrectable);
        mask(cfgWmask, 0x10C, uncorrectable);
        mask(cfgWmask, 0x114, correctable);
    }
    cfgReset = cfg;
}

/** 硬件更新只读状态，size 为 1、2 或 4。 */
void
ConfigSpace::setLive(Addr off, unsigned size, uint32_t value)
{
    for (unsigned i = 0; i < size; ++i) {
        cfg[off + i] = value >> (8 * i);
    }
}

/** FLR 复位功能字段，保留规范规定的链路和 sticky AER 字段。 */
void
ConfigSpace::resetFunction()
{
    CfgBytes old = cfg;
    cfg = cfgReset;
    cfg[0x58] = (cfg[0x58] & ~0xE0) | (old[0x58] & 0xE0);
    for (Addr off : {0x60, 0x61, 0x80, 0x81}) {
        cfg[off] = old[off];
    }
    if (linkExtensions) {
        std::copy(old.begin() + 0x100, old.end(), cfg.begin() + 0x100);
    }
}

/** 记录 AER 并返回应发送的错误 Message Code，零表示无需上报。 */
uint8_t
ConfigSpace::raiseAer(pcie::LinkError error,
                      const std::vector<uint8_t> &header)
{
    using pcie::LinkError;
    bool correctable = false;
    unsigned bit = 0;
    switch (error) {
        case LinkError::Receiver:
            correctable = true;
            bit = 0;
            break;
        case LinkError::BadTlp:
            correctable = true;
            bit = 6;
            break;
        case LinkError::BadDllp:
            correctable = true;
            bit = 7;
            break;
        case LinkError::ReplayRollover:
            correctable = true;
            bit = 8;
            break;
        case LinkError::ReplayTimeout:
            correctable = true;
            bit = 12;
            break;
        case LinkError::DataLinkProtocol:
            bit = 4;
            break;
        case LinkError::SurpriseDown:
            bit = 5;
            break;
        case LinkError::Poisoned:
            bit = 12;
            break;
        case LinkError::FlowControl:
            bit = 13;
            break;
        case LinkError::CompletionTimeout:
            bit = 14;
            break;
        case LinkError::CompleterAbort:
            bit = 15;
            break;
        case LinkError::UnexpectedCompletion:
            bit = 16;
            break;
        case LinkError::ReceiverOverflow:
            bit = 17;
            break;
        case LinkError::Malformed:
            bit = 18;
            break;
        case LinkError::UnsupportedRequest:
            bit = 20;
            break;
        case LinkError::TrainingTimeout:
            return 0;
    }
    uint32_t flag = 1u << bit;
    Addr status = correctable ? 0x110 : 0x104;
    uint32_t prior = read(status, 4);
    bool masked = read(correctable ? 0x114 : 0x108, 4) & flag;
    if (linkExtensions) {
        setLive(status, 4, prior | flag);
        if (!correctable && !masked && !(prior & ~read(0x108, 4))) {
            setLive(0x118, 4, (read(0x118, 4) & ~31u) | bit);
            for (unsigned i = 0; i < 16; ++i) {
                cfg[0x11C + i] = (i ^ 3) < header.size() ? header[i ^ 3] : 0;
            }
        }
    }
    bool fatal = !correctable && (read(0x10C, 4) & flag);
    unsigned device_bit = correctable ? 0 : fatal ? 2 : 1;
    uint32_t detected = 1u << device_bit;
    if (error == LinkError::UnsupportedRequest) {
        detected |= 8;
    }
    setLive(0x5A, 2, read(0x5A, 2) | detected);
    uint32_t enable = read(0x58, 2);
    bool report = enable & (1u << device_bit);
    if (error == LinkError::UnsupportedRequest) {
        report &= enable & 8;
    }
    return masked || !report ? 0 : correctable ? 0x30 : fatal ? 0x33 : 0x31;
}

/** 读 off 起的 size 字节（小端序）；off 不小于 OwnBase */
uint32_t
ConfigSpace::read(Addr off, unsigned size) const
{
    uint32_t val = 0;
    for (unsigned i = 0; i < size; i++)
        val |= uint32_t(cfg[off + i]) << (8 * i);
    return val;
}

/**
 * 写 off 起的 size 字节：按可写位和写 1 清零位更新，再交给各
 * capability 处理；返回需要设备处理的事件
 */
CfgEvent
ConfigSpace::write(Addr off, unsigned size, uint32_t val)
{
    return writeMasked(off, size, val, std::vector<bool>(size, true));
}

/** 按 Byte Enable 写配置，禁用字节不触发 RW1C 或 FLR。 */
CfgEvent
ConfigSpace::writeMasked(Addr off, unsigned size, uint32_t val,
                         const std::vector<bool> &mask)
{
    const CfgBytes old = cfg;
    for (unsigned i = 0; i < size; i++) {
        if (!mask.at(i)) {
            val &= ~(255u << (8 * i));
            continue;
        }
        const Addr b = off + i;
        const uint8_t byte = uint8_t(val >> (8 * i));
        cfg[b] = (cfg[b] & ~cfgWmask[b]) | (byte & cfgWmask[b]);
        cfg[b] &= ~(byte & cfgW1c[b]);
    }

    // FLR 优先于电源状态的变化
    CfgEvent ev = CfgEvent::None;
    for (const auto &e : caps) {
        const CfgEvent r = e.cap->afterWrite(old, cfg, e.base, off, size, val);
        if (r == CfgEvent::Flr)
            ev = r;
        else if (r != CfgEvent::None && ev == CfgEvent::None)
            ev = r;
    }
    return ev;
}

/** 保存配置空间映像的当前值 */
void
ConfigSpace::serialize(CheckpointOut &cp) const
{
    SERIALIZE_ARRAY(cfg.data(), cfg.size());
}

/** 恢复 serialize() 保存的映像 */
void
ConfigSpace::unserialize(CheckpointIn &cp)
{
    UNSERIALIZE_ARRAY(cfg.data(), cfg.size());
}

} // namespace gw100
} // namespace gem5
