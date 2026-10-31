/**
 * @file  pcie_types.hh
 * @brief GW100 PCIe 各组件共用的接口与类型
 *
 * Gw100PciDevice 由若干组件组成（Gw100PcieFactory 负责组装）。组件之间
 * 只经本文件定义的接口交互：BAR 处理者处理一次 BAR 访问，寄存器块响应
 * BAR0 中自己那一块的读写，配置写的结果以 CfgEvent 通知外壳。内部组件
 * 不接触 gem5 的 Packet，便于单元测试。
 *
 * @see spec-pcie
 */

#ifndef __GW100_PCIE_PCIE_TYPES_HH__
#define __GW100_PCIE_PCIE_TYPES_HH__

#include <array>
#include <cstdint>

#include "base/types.hh"
#include "sim/serialize.hh"

namespace gem5
{
namespace gw100
{

/** 一次 BAR 访问：BAR 内偏移、字节数、方向和数据缓冲 */
struct BarAccess
{
    /** BAR 内的偏移 */
    Addr off;
    /** 访问的字节数 */
    unsigned size;
    /** true 表示写，false 表示读 */
    bool write;
    /** 数据缓冲：写时为待写入的数据，读时由处理者填入读到的数据 */
    uint8_t *data;
};

/** 一个 BAR 的处理者：负责这个 BAR 内全部地址的读和写 */
class BarHandler
{
  public:
    /** 虚析构，允许经基类指针销毁 */
    virtual ~BarHandler() = default;

    /**
     * 处理一次访问，返回这次访问的设备侧延迟。访问总是以成功完成结束，
     * 错误情形由处理者记录（spec-pcie §5.2、§7）
     */
    virtual Tick access(BarAccess &a) = 0;
};

/** BAR0 中的一个寄存器块，占据从 base() 起的一段偏移 */
class RegBlock : public Serializable
{
  public:
    /** 虚析构，允许经基类指针销毁 */
    virtual ~RegBlock() = default;

    /** 块在 BAR0 中的起始偏移 */
    virtual Addr base() const = 0;

    /** BAR0 偏移 off 处的 32 位字是否是本块已实现的寄存器 */
    virtual bool implemented(Addr off) const = 0;

    /** 读 BAR0 偏移 off 起的 size 字节到 data；调用者已检查宽度和偏移 */
    virtual void read(Addr off, uint8_t *data, unsigned size) = 0;

    /** 把 data 中的 size 字节写到 BAR0 偏移 off 起；调用者已检查 */
    virtual void write(Addr off, const uint8_t *data, unsigned size) = 0;

    /** 把本块的全部状态恢复为复位值 */
    virtual void reset() = 0;
};

/** 配置空间（含扩展部分）的大小 */
constexpr Addr CfgSpaceSize = 0x1000;

/** 配置空间的字节映像 */
using CfgBytes = std::array<uint8_t, CfgSpaceSize>;

/** 一次配置写对设备的影响，由外壳据此执行复位 */
enum class CfgEvent
{
    /** 没有需要处理的事件 */
    None,
    /** host 写了 Device Control 的 Initiate FLR */
    Flr,
    /** 电源状态由 D3hot 回到 D0（No_Soft_Reset = 0，需要复位） */
    D3hotToD0,
};

} // namespace gw100
} // namespace gem5

#endif // __GW100_PCIE_PCIE_TYPES_HH__
