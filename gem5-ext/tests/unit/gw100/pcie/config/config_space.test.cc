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
#include "src/gw100/pcie/link/link_model.hh"

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

/** PMCSR：D0 → D3hot 与 D3hot → D0 各产生对应事件 */
TEST_F(ConfigSpaceTest, D3hotRoundTrip)
{
    EXPECT_EQ(cs.write(0x44, 2, 3), CfgEvent::D0ToD3hot);
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

/** 精确配置的扩展能力链、AER RW1C 和 FLR 保留字段。 */
TEST(ConfigSpaceLink, AerAndFlrPreservation)
{
    ConfigSpace config;
    config.addCapability(0x50, std::make_unique<PcieCapability>(4, 4));
    config.enableLinkExtensions();
    config.finalize();
    EXPECT_EQ(config.read(0x100, 4), 0x14010001u);
    EXPECT_EQ(config.read(0x140, 4), 0x18010019u);
    EXPECT_EQ(config.read(0x180, 4), 0x1A010025u);
    EXPECT_EQ(config.read(0x1A0, 4), 0x00010026u);
    config.write(0x58, 2, config.read(0x58, 2) | 0x27);
    config.write(0x60, 2, 0x82);
    EXPECT_EQ(config.raiseAer(pcie::LinkError::BadTlp, {}), 0x30);
    EXPECT_EQ(config.read(0x110, 4), 1u << 6);
    config.resetFunction();
    EXPECT_EQ(config.read(0x58, 2) & 0xE0, 0x20);
    EXPECT_EQ(config.read(0x60, 2), 0x82);
    EXPECT_EQ(config.read(0x110, 4), 1u << 6);
    config.write(0x110, 4, 1u << 6);
    EXPECT_EQ(config.read(0x110, 4), 0);
}

/** 禁用高字节不能触发 FLR，也不能清除该字节中的 RW1C 状态。 */
TEST(ConfigSpaceLink, ByteEnableProtectsSideEffects)
{
    ConfigSpace config;
    config.addCapability(0x50, std::make_unique<PcieCapability>(4, 4));
    config.enableLinkExtensions();
    config.finalize();
    std::vector<bool> low_byte{true, false};
    EXPECT_EQ(config.writeMasked(0x58, 2, 0x8020, low_byte), CfgEvent::None);
    EXPECT_EQ(config.read(0x58, 2) & 0xE0, 0x20);
    config.setLive(0x104, 4, 1u << 20);
    config.writeMasked(0x104, 4, 1u << 20, {true, true, false, true});
    EXPECT_EQ(config.read(0x104, 4), 1u << 20);
}

/** 单 PF Endpoint 的 Crosslink 控制位按规范保留。 */
TEST(ConfigSpaceLink, SecondaryEndpointControlsReserved)
{
    ConfigSpace config;
    config.addCapability(0x50, std::make_unique<PcieCapability>(4, 4));
    config.enableLinkExtensions();
    config.finalize();
    config.write(0x144, 4, 3);
    EXPECT_EQ(config.read(0x144, 4), 0);
}

/** A/B 范围的合法超时选择生效，保留选择保持先前值。 */
TEST_F(ConfigSpaceTest, CompletionTimeoutSelection)
{
    for (unsigned selection : {0, 1, 2, 5, 6}) {
        cs.write(0x78, 2, selection);
        EXPECT_EQ(cs.read(0x78, 2), selection);
        cs.write(0x78, 2, 3);
        EXPECT_EQ(cs.read(0x78, 2), selection);
    }
}
