/**
 * @file  bar0_handler.test.cc
 * @brief BAR0 字节使能和非法写入的副作用检查
 *
 * @see spec-pcie
 */

#include <gtest/gtest.h>

#include <cstring>

#include "src/gw100/pcie/bar0/bar0_handler.hh"

using namespace gem5;
using namespace gem5::gw100;

/** 记录实际寄存器写入的最小目标。 */
class TestRegister : public RegBlock
{
  public:
    /** 本块在 BAR0 中的起始偏移。 */
    Addr
    base() const override
    {
        return 0;
    }
    /** 仅实现偏移零的 DWORD。 */
    bool
    implemented(Addr off) const override
    {
        return off == 0;
    }
    /** 读取当前 DWORD 的有效字节。 */
    void
    read(Addr off, uint8_t *data, unsigned size) override
    {
        std::memcpy(data, reinterpret_cast<uint8_t *>(&value) + off, size);
    }
    /** 记录前端允许执行的写操作。 */
    void
    write(Addr, const uint8_t *data, unsigned size) override
    {
        std::memcpy(&value, data, size);
        ++writes;
    }
    /** 清除数据与副作用计数。 */
    void
    reset() override
    {
        value = 0;
        writes = 0;
    }
    /** 此夹具的目标无需保存外部状态。 */
    void
    serialize(CheckpointOut &) const override
    {}
    /** 此夹具的目标无需恢复外部状态。 */
    void
    unserialize(CheckpointIn &) override
    {}
    /** 目标 DWORD。 */
    uint32_t value = 0;
    /** 实际写入次数。 */
    unsigned writes = 0;
};

/** 稀疏 Byte Enable 按 GW100 BAR0 完整 DWORD 写契约报告错误。 */
TEST(Bar0Handler, SparseWriteCannotModifyDisabledBytes)
{
    StatusUnit status("test");
    Bar0Handler bar(status, 1000);
    TestRegister target;
    bar.addBlock(target);
    uint32_t value = 0xa5a5a5a5;
    std::vector<bool> mask{true, false, false, true};
    BarAccess access{0, 4, true, reinterpret_cast<uint8_t *>(&value), &mask};
    EXPECT_EQ(bar.access(access), 1000);
    EXPECT_EQ(target.writes, 0);
    EXPECT_EQ(status.errStatus(), StatusUnit::ErrBadWidth);
    mask.assign(4, true);
    bar.access(access);
    EXPECT_EQ(target.value, value);
    EXPECT_EQ(target.writes, 1);
}
