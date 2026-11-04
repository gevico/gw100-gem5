/**
 * @file  hdma_mirror.hh
 * @brief BAR0 的 Host DMA 寄存器只读镜像（0x6000）
 *
 * HDMA 寄存器文件的前 4 KiB 映射到 BAR0 偏移 0x6000 – 0x6FFF，偏移一一
 * 对应；读返回与固件看到的相同的值，读低位锁存高位的寄存器与固件的
 * 锁存互相独立；写入被忽略（spec-host-dma §7.4）。
 *
 * @see spec-host-dma, spec-pcie
 */

#ifndef __GW100_PCIE_BAR0_HDMA_MIRROR_HH__
#define __GW100_PCIE_BAR0_HDMA_MIRROR_HH__

#include "src/gw100/pcie/hdma/hdma_engine.hh"
#include "src/gw100/pcie/pcie_types.hh"

namespace gem5
{
namespace gw100
{

/** Host DMA 寄存器的只读镜像 */
class HdmaMirror : public RegBlock
{
  public:
    /** 本块在 BAR0 中的起始偏移 */
    static constexpr Addr Base = 0x6000;

    /** 构造：hdma 为被镜像的 HDMA */
    explicit HdmaMirror(HdmaEngine &hdma) : hdma(hdma) {}

    /** 块在 BAR0 中的起始偏移 */
    Addr base() const override { return Base; }

    /** BAR0 偏移 off 处的 32 位字是否是 HDMA 已实现的寄存器 */
    bool implemented(Addr off) const override
    { return hdma.implemented(off - Base); }

    /** 读 BAR0 偏移 off 起的 size 字节 */
    void read(Addr off, uint8_t *data, unsigned size) override;

    /** 只读：写入被忽略 */
    void write(Addr, const uint8_t *, unsigned) override {}

    /** 没有自己的状态 */
    void reset() override {}

    /** 没有自己的状态 */
    void serialize(CheckpointOut &) const override {}

    /** 没有自己的状态 */
    void unserialize(CheckpointIn &) override {}

  private:
    /** 被镜像的 HDMA */
    HdmaEngine &hdma;
};

} // namespace gw100
} // namespace gem5

#endif // __GW100_PCIE_BAR0_HDMA_MIRROR_HH__
