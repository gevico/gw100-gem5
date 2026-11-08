/**
 * @file  mgmt_rom.hh
 * @brief 管理 Tile 独立启动 ROM
 * @see spec-mgmt-tile
 */
#pragma once
#include <cstdint>
#include <vector>
#include "base/types.hh"

namespace gem5::gw100
{
/** 独立只读启动窗口，功能写仅用于加载启动镜像。 */
class MgmtRom
{
  public:
    /** 管理 CPU 本地 ROM 基地址。 */
    static constexpr Addr RomBase = 0;
    /** 分配固定字节容量的启动 ROM。 */
    explicit MgmtRom(uint64_t size) : romSize(size), bytes(size) {}
    /** 检查非空的完整 ROM 范围。 */
    bool
    contains(Addr address, unsigned size) const
    { return size && address < romSize && size <= romSize - address; }
    /** 返回已完成范围检查的 ROM 字节指针。 */
    uint8_t *
    ptr(Addr address)
    { return bytes.data() + address; }
    /** ROM 数据容量，单位为字节。 */
    const uint64_t romSize;

  private:
    /** 独立的启动镜像存储。 */
    std::vector<uint8_t> bytes;
};
} // namespace gem5::gw100
