/**
 * @file  capability.hh
 * @brief 配置空间中 PCI capability 的基类
 *
 * 每个 capability 只负责自己那一段：初值、可写位、写 1 清零位，以及写入
 * 之后需要设备处理的事件。capability ID 和链表的 next 指针由
 * ConfigSpace 填写。本目录下每个子类一个文件；以后的 MSI-X、AER 等照此
 * 添加，并在 Gw100PcieFactory 中注册。
 *
 * @see spec-pcie
 */

#ifndef __GW100_PCIE_CONFIG_CAPABILITY_CAPABILITY_HH__
#define __GW100_PCIE_CONFIG_CAPABILITY_CAPABILITY_HH__

#include <cstdint>

#include "base/types.hh"
#include "src/gw100/pcie/pcie_types.hh"

namespace gem5
{
namespace gw100
{

/** 配置空间中的一个 capability */
class Capability
{
  public:
    /** 虚析构，允许经基类指针销毁 */
    virtual ~Capability() = default;

    /** capability ID */
    virtual uint8_t id() const = 0;

    /**
     * 在 base 起写本 capability 的初值、可写位和写 1 清零位；
     * base 和 base + 1 的 ID 与 next 指针由 ConfigSpace 填写
     */
    virtual void init(CfgBytes &cfg, CfgBytes &wmask, CfgBytes &w1c,
                      Addr base) const = 0;

    /**
     * 一次配置写按可写位和写 1 清零位生效之后调用：old 是写入前的映像，
     * cfg 是写入后的映像（可以修改，例如撤销不支持的取值）；off、size、
     * val 是这次写。返回需要设备处理的事件
     */
    virtual CfgEvent afterWrite(const CfgBytes &old, CfgBytes &cfg,
                                Addr base, Addr off, unsigned size,
                                uint32_t val) const = 0;

  protected:
    /** 以小端序把 16 位值写到 a[off] 起的两个字节 */
    static void
    put16(CfgBytes &a, Addr off, uint16_t v)
    {
        a[off] = v & 0xFF;
        a[off + 1] = v >> 8;
    }

    /** 以小端序把 32 位值写到 a[off] 起的四个字节 */
    static void
    put32(CfgBytes &a, Addr off, uint32_t v)
    {
        for (int i = 0; i < 4; i++)
            a[off + i] = (v >> (8 * i)) & 0xFF;
    }
};

} // namespace gw100
} // namespace gem5

#endif // __GW100_PCIE_CONFIG_CAPABILITY_CAPABILITY_HH__
