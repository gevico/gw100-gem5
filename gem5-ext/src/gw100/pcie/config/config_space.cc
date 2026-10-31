/**
 * @file  config_space.cc
 * @brief ConfigSpace 的实现
 *
 * @see spec-pcie
 */

#include "src/gw100/pcie/config/config_space.hh"

#include <utility>

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
    // 扩展配置空间 0x100：头为 0，表示没有扩展 capability
    cfgReset = cfg;
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
    const CfgBytes old = cfg;
    for (unsigned i = 0; i < size; i++) {
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
