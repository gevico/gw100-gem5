/**
 * @file  config_space.hh
 * @brief 配置空间 0x40 起的部分：capability 链表与扩展配置空间
 *
 * gem5 的 PciDevice 只提供 0x00–0x3F 的头部，不暴露 0x40 以上的内容，
 * 也不支持扩展配置空间。本类按 12 位偏移保存 0x40–0xFFF 的字节映像、
 * 可写位和写 1 清零位，并按注册顺序把 capability 连成链表。
 *
 * @see spec-pcie
 */

#ifndef __GW100_PCIE_CONFIG_CONFIG_SPACE_HH__
#define __GW100_PCIE_CONFIG_CONFIG_SPACE_HH__

#include <cstdint>
#include <memory>
#include <vector>

#include "base/types.hh"
#include "sim/serialize.hh"
#include "src/gw100/pcie/config/capability/capability.hh"
#include "src/gw100/pcie/pcie_types.hh"

namespace gem5
{
namespace gw100
{

/** 配置空间中由 GW100 自己提供的部分（0x40–0xFFF） */
class ConfigSpace : public Serializable
{
  public:
    /** 从这个偏移起的配置空间由本类提供 */
    static constexpr Addr OwnBase = 0x40;

    /**
     * 在 base 处注册一个 capability；链表按注册顺序连接。必须在
     * finalize() 之前调用
     */
    void addCapability(Addr base, std::unique_ptr<Capability> cap);

    /** 填写各 capability 的 ID、next 指针和初值，并保存为复位值 */
    void finalize();

    /** 读 off 起的 size 字节（小端序）；off 不小于 OwnBase */
    uint32_t read(Addr off, unsigned size) const;

    /**
     * 写 off 起的 size 字节：按可写位和写 1 清零位更新，再交给各
     * capability 处理；返回需要设备处理的事件
     */
    CfgEvent write(Addr off, unsigned size, uint32_t val);

    /** 恢复为 finalize() 时保存的复位值 */
    void reset() { cfg = cfgReset; }

    /** 保存配置空间映像的当前值 */
    void serialize(CheckpointOut &cp) const override;

    /** 恢复 serialize() 保存的映像 */
    void unserialize(CheckpointIn &cp) override;

  private:
    /** 一个已注册的 capability 及其位置 */
    struct Entry
    {
        /** capability 在配置空间中的起始偏移 */
        Addr base;
        /** capability 本身 */
        std::unique_ptr<Capability> cap;
    };

    /** 按注册顺序排列的 capability */
    std::vector<Entry> caps;
    /** 配置空间映像的当前值 */
    CfgBytes cfg{};
    /** 配置空间映像的复位值 */
    CfgBytes cfgReset{};
    /** 可写的位 */
    CfgBytes cfgWmask{};
    /** 写 1 清零的位 */
    CfgBytes cfgW1c{};
};

} // namespace gw100
} // namespace gem5

#endif // __GW100_PCIE_CONFIG_CONFIG_SPACE_HH__
