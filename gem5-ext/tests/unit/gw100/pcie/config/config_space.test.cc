/**
 * @file  config_space.test.cc
 * @brief ConfigSpace 与 PM、PCI Express capability 的单元测试
 *
 * @see spec-pcie
 */

#include <gtest/gtest.h>

#include <memory>

#include "src/gw100/pcie/config/capability/pcie_capability.hh"
#include "src/gw100/pcie/config/capability/pm_capability.hh"
#include "src/gw100/pcie/config/config_space.hh"

using namespace gem5;
using namespace gem5::gw100;

namespace
{

/** 按 spec-pcie §4.2 组装的配置空间：PM 在 0x40，PCI Express 在 0x50 */
class ConfigSpaceTest : public ::testing::Test
{
  protected:
    /** 注册两个 capability（Gen4 x4）并完成初始化 */
    void
    SetUp() override
    {
        cs.addCapability(0x40, std::make_unique<PmCapability>());
        cs.addCapability(0x50, std::make_unique<PcieCapability>(4, 4));
        cs.finalize();
    }

    /** 被测的配置空间 */
    ConfigSpace cs;
};

} // anonymous namespace

/** capability 链表：PM → PCI Express → 结束 */
TEST_F(ConfigSpaceTest, CapabilityChain)
{
    EXPECT_EQ(cs.read(0x40, 1), 0x01u);
    EXPECT_EQ(cs.read(0x41, 1), 0x50u);
    EXPECT_EQ(cs.read(0x50, 1), 0x10u);
    EXPECT_EQ(cs.read(0x51, 1), 0x00u);
}

/** 只读字段的写入被忽略；Link Status 反映链路参数 */
TEST_F(ConfigSpaceTest, ReadOnlyFields)
{
    const uint32_t devcap = cs.read(0x54, 4);
    EXPECT_EQ(cs.write(0x54, 4, 0), CfgEvent::None);
    EXPECT_EQ(cs.read(0x54, 4), devcap);
    EXPECT_EQ(cs.read(0x62, 2), 4u | (4u << 4) | (1u << 12));
}

/** PMCSR：D0 → D3hot 无事件，D3hot → D0 产生 D3hotToD0 */
TEST_F(ConfigSpaceTest, D3hotRoundTrip)
{
    EXPECT_EQ(cs.write(0x44, 2, 3), CfgEvent::None);
    EXPECT_EQ(cs.read(0x44, 2) & 0x3, 3u);
    EXPECT_EQ(cs.write(0x44, 2, 0), CfgEvent::D3hotToD0);
    EXPECT_EQ(cs.read(0x44, 2) & 0x3, 0u);
}

/** 不支持 D1、D2：写入后 PowerState 保持原值 */
TEST_F(ConfigSpaceTest, D1D2Ignored)
{
    EXPECT_EQ(cs.write(0x44, 2, 1), CfgEvent::None);
    EXPECT_EQ(cs.read(0x44, 2) & 0x3, 0u);
    EXPECT_EQ(cs.write(0x44, 2, 2), CfgEvent::None);
    EXPECT_EQ(cs.read(0x44, 2) & 0x3, 0u);
}

/** Initiate FLR 产生 Flr 事件，读回为 0；同一次写的可写位照常生效 */
TEST_F(ConfigSpaceTest, InitiateFlr)
{
    const uint32_t devctl = cs.read(0x58, 2);
    EXPECT_EQ(cs.write(0x58, 2, devctl | 0x8000 | (1u << 5)),
              CfgEvent::Flr);
    EXPECT_EQ(cs.read(0x58, 2) & 0x8000, 0u);
    EXPECT_EQ(cs.read(0x58, 2) & (1u << 5), 1u << 5);
}

/** reset() 恢复 finalize() 时的值 */
TEST_F(ConfigSpaceTest, Reset)
{
    const uint32_t devctl = cs.read(0x58, 2);
    cs.write(0x58, 2, 0);
    cs.write(0x44, 2, 3);
    cs.reset();
    EXPECT_EQ(cs.read(0x58, 2), devctl);
    EXPECT_EQ(cs.read(0x44, 2), 0u);
}
