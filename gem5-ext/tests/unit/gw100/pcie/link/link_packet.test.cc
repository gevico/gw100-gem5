/**
 * @file  link_packet.test.cc
 * @brief 验证标准 PCIe 包字段、CRC 和事务排序
 *
 * 定值向量按原文头部布局和独立多项式长除法形成。
 *
 * @see spec-pcie
 */

#include <gtest/gtest.h>

#include "src/gw100/pcie/link/generated/link_defs.hh"
#include "src/gw100/pcie/link/packet.hh"

using namespace gem5::gw100;
using namespace gem5::gw100::pcie;

/** 32 位 CRC 定值同时约束输入位序和发送字节序。 */
TEST(LinkPacket, Crc32WireVector)
{
    std::vector<uint8_t> input{'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    EXPECT_EQ(crc(input, 32, pciedefs::LCRC_POLYNOMIAL),
              (std::vector<uint8_t>{0x26, 0x39, 0xf4, 0xcb}));
}

/** 16 位 CRC 使用 PCIe 专用多项式，四字节零内容的定值。 */
TEST(LinkPacket, Crc16WireVector)
{
    EXPECT_EQ(crc({0, 0, 0, 0}, 16, pciedefs::DLLP_CRC_POLYNOMIAL),
              (std::vector<uint8_t>{0xb3, 0x62}));
}

/** 32 位内存地址形成三 DWORD 标准头。 */
TEST(LinkPacket, MemoryWriteHeader)
{
    const uint8_t data[] = {1, 2, 3, 4};
    auto request =
        Tlp::request(TlpKind::MemoryWrite, 0x12345678, 4, 0x1234, 0x56, data);
    EXPECT_EQ(request.encode(),
              (std::vector<uint8_t>{0x40, 0, 0, 1, 0x12, 0x34, 0x56, 0x0f,
                                    0x12, 0x34, 0x56, 0x78, 1, 2, 3, 4}));
    EXPECT_TRUE(request.validate(128, 512).empty());
}

/** 高于 4 GiB 的内存地址形成四 DWORD 头。 */
TEST(LinkPacket, MemoryRead64BitHeader)
{
    auto request =
        Tlp::request(TlpKind::MemoryRead, 0x100001004, 64, 0xabcd, 7);
    EXPECT_EQ(request.encode(),
              (std::vector<uint8_t>{0x20, 0, 0, 16, 0xab, 0xcd, 7, 0xff, 0, 0,
                                    0, 1, 0, 0, 0x10, 4}));
}

/** 配置写为带数据的 non-posted，保留目标 BDF 和扩展偏移。 */
TEST(LinkPacket, ConfigurationWrite)
{
    const uint8_t data[] = {3, 0, 0, 0};
    auto request = Tlp::request(TlpKind::ConfigWrite, 0x94, 4, 0, 3, data);
    request.destination = 0x100;
    EXPECT_EQ(request.encode(),
              (std::vector<uint8_t>{0x44, 0, 0, 1, 0, 0, 3, 15, 1, 0, 0, 0x94,
                                    3, 0, 0, 0}));
    EXPECT_EQ(request.traffic(), Traffic::NonPosted);
}

/** Byte Enable 和完成跨度覆盖自然非对齐访问。 */
TEST(LinkPacket, UnalignedBytes)
{
    for (unsigned offset = 0; offset < 4; ++offset) {
        for (unsigned size = 1; size <= 512; ++size) {
            auto request =
                Tlp::request(TlpKind::MemoryRead, 0x1000 + offset, size, 1, 2);
            EXPECT_EQ(request.firstOffset(), offset);
            EXPECT_EQ(request.requestBytes(), size);
            for (unsigned byte = 0; byte < request.length * 4; ++byte) {
                EXPECT_EQ(request.byteEnabled(byte),
                          byte >= offset && byte < offset + size);
            }
            EXPECT_TRUE(request.validate(256, 1024).empty());
        }
    }
}

/** 非对齐读的第一个完成带 DWORD 前缀，Byte Count 逐段递减。 */
TEST(LinkPacket, CompletionHeaderAndPadding)
{
    auto request = Tlp::request(TlpKind::MemoryRead, 0x103, 9, 0x1234, 3);
    auto first = Tlp::completion(request, 0x5678, 0, {0x33});
    EXPECT_EQ(first.encode(),
              (std::vector<uint8_t>{0x4a, 0, 0, 1, 0x56, 0x78, 0, 9, 0x12,
                                    0x34, 3, 3, 0, 0, 0, 0x33}));
    auto last =
        Tlp::completion(request, 0x5678, 1, {4, 5, 6, 7, 8, 9, 10, 11});
    EXPECT_EQ(last.lowerAddress, 4);
    EXPECT_EQ(last.byteCount, 8);
    EXPECT_EQ(last.payload.size(), 8);
}

/** UR 和 CA 是无载荷终止完成。 */
TEST(LinkPacket, ErrorCompletion)
{
    auto request = Tlp::request(TlpKind::MemoryRead, 0x1000, 64, 1, 7);
    auto completion = Tlp::completion(request, 2, 0, {}, CompletionStatus::Ur);
    EXPECT_EQ(completion.encode().size(), 12);
    EXPECT_TRUE(completion.validate(128, 512).empty());
    completion.payload = {1, 2, 3, 4};
    EXPECT_FALSE(completion.validate(128, 512).empty());
}

/** Gen3/4 STP 的任意单 bit 破坏均被校验发现。 */
TEST(LinkPacket, StartTokenProtection)
{
    for (unsigned length = 5; length < 1152; ++length) {
        unsigned decoded_length = 0;
        uint16_t sequence = 0;
        auto token = startToken(length, 0xabc);
        ASSERT_TRUE(checkStartToken(token, decoded_length, sequence));
        ASSERT_EQ(decoded_length, length);
        ASSERT_EQ(sequence, 0xabc);
        // 序号由 LCRC 保护，这里只破坏 STP 的长度、CRC 和 Parity。
        for (unsigned bit = 0; bit < 20; ++bit) {
            auto corrupt = token;
            unsigned byte = bit / 8;
            unsigned shift = bit % 8;
            if (byte == 2) {
                shift += 4;
            }
            corrupt[byte] ^= 1u << shift;
            ASSERT_FALSE(checkStartToken(corrupt, decoded_length, sequence));
        }
    }
}

/** FC 字段的边界、扩展尺度和十二位回绕编码完整。 */
TEST(LinkPacket, FlowControlFields)
{
    for (unsigned scale = 0; scale < 4; ++scale) {
        auto fc = Dllp::flowControl(0x90, 255, 4095, scale, scale);
        EXPECT_EQ(fc.headerCredit(), 255);
        EXPECT_EQ(fc.dataCredit(), 4095);
        EXPECT_EQ(fc.headerScale(), scale);
        EXPECT_EQ(fc.dataScale(), scale);
        Dllp decoded;
        EXPECT_TRUE(Dllp::decode(fc.encode(), decoded));
        EXPECT_EQ(decoded.data, fc.data);
    }
}

/** 已验证 CRC 后仍独立检查完整头和载荷长度。 */
TEST(LinkPacket, MalformedLength)
{
    Tlp decoded;
    std::string error;
    auto request = Tlp::request(TlpKind::MemoryRead, 0x1000, 64, 1, 2);
    auto bytes = request.encode();
    bytes[0] |= 0x40;
    EXPECT_FALSE(Tlp::decode(bytes, decoded, error));
    EXPECT_FALSE(error.empty());
}

/** replay 数据含序号和 LCRC，内容破坏会失败。 */
TEST(LinkPacket, DataLinkIntegrity)
{
    auto request = Tlp::request(TlpKind::MemoryRead, 0x1000, 64, 1, 2);
    auto bytes = dataLinkPacket(request, 4095);
    uint16_t sequence = 0;
    std::vector<uint8_t> body;
    EXPECT_TRUE(checkDataLinkPacket(bytes, sequence, body));
    EXPECT_EQ(sequence, 4095);
    EXPECT_EQ(body, request.encode());
    bytes[3] ^= 1;
    EXPECT_FALSE(checkDataLinkPacket(bytes, sequence, body));
}

/** P、NP 和完成的越过规则与 RO、No Snoop 的含义分开。 */
TEST(LinkPacket, Ordering)
{
    const uint8_t data[4] = {};
    auto posted = Tlp::request(TlpKind::MemoryWrite, 0x1000, 4, 1, 0, data);
    auto read = Tlp::request(TlpKind::MemoryRead, 0x1000, 4, 1, 1);
    auto completion = Tlp::completion(read, 2, 0, {1, 2, 3, 4});
    EXPECT_FALSE(mayPass(read, posted));
    read.attributes = 3;
    EXPECT_FALSE(mayPass(read, posted));
    EXPECT_TRUE(mayPass(posted, read));
    EXPECT_TRUE(mayPass(completion, read));
    EXPECT_FALSE(mayPass(completion, posted));
    completion.attributes = 2;
    EXPECT_TRUE(mayPass(completion, posted));
    auto next = completion;
    EXPECT_FALSE(mayPass(next, completion));
    ++next.tag;
    EXPECT_TRUE(mayPass(next, completion));
}

/** 配置读写完成的 Byte Count 固定为四，Lower Address 固定为零。 */
TEST(LinkPacket, ConfigurationCompletionFields)
{
    auto read = Tlp::request(TlpKind::ConfigRead, 0x43, 1, 0, 7);
    auto completion = Tlp::completion(read, 8, 0, {0xa5});
    EXPECT_EQ(completion.byteCount, 4);
    EXPECT_EQ(completion.lowerAddress, 0);
    EXPECT_EQ(completion.payload.size(), 4);
    EXPECT_EQ(completion.payload[3], 0xa5);
    uint8_t value = 0x5a;
    auto write = Tlp::request(TlpKind::ConfigWrite, 0x43, 1, 0, 7, &value);
    auto acknowledge = Tlp::completion(write, 8, 0, {});
    EXPECT_EQ(acknowledge.byteCount, 4);
    EXPECT_EQ(acknowledge.lowerAddress, 0);
    EXPECT_TRUE(acknowledge.payload.empty());
}

/** 零长度 Memory Read 返回一 DWORD，Byte Count 按表 2-37 为一。 */
TEST(LinkPacket, ZeroLengthReadCompletion)
{
    auto request = Tlp::request(TlpKind::MemoryRead, 0x1000, 4, 0, 1);
    request.firstBe = 0;
    auto completion =
        Tlp::completion(request, 8, 0, std::vector<uint8_t>(4, 0));
    EXPECT_EQ(completion.length, 1);
    EXPECT_EQ(completion.byteCount, 1);
    EXPECT_EQ(completion.payload.size(), 4);
}
