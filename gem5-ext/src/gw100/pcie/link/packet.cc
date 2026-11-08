/**
 * @file  packet.cc
 * @brief PCIe 包格式和完整性编码的实现
 *
 * @see spec-pcie
 */

#include "src/gw100/pcie/link/packet.hh"

#include <algorithm>
#include <stdexcept>

#include "src/gw100/pcie/link/generated/link_defs.hh"

namespace gem5::gw100::pcie
{

namespace
{

/** 在标准头部追加一个高字节先行的 DWORD。 */
void
appendDw(std::vector<uint8_t> &bytes, uint32_t value)
{
    for (int shift = 24; shift >= 0; shift -= 8) {
        bytes.push_back(value >> shift);
    }
}

/** 从已检查长度的标准头部读取一个 DWORD。 */
uint32_t
readDw(const std::vector<uint8_t> &bytes, unsigned offset)
{
    uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i) {
        value = (value << 8) | bytes[offset + i];
    }
    return value;
}

/** 返回位向量的奇偶校验值。 */
unsigned
parity(unsigned value)
{
    return __builtin_parity(value);
}

} // anonymous namespace

/** 返回对应的流控类别。 */
Traffic
Tlp::traffic() const
{
    if (kind == TlpKind::Completion) {
        return Traffic::Completion;
    }
    if (kind == TlpKind::MemoryWrite || kind == TlpKind::Message) {
        return Traffic::Posted;
    }
    return Traffic::NonPosted;
}

/** 是否为携带地址与 Byte Enable 的请求。 */
bool
Tlp::isRequest() const
{
    return kind == TlpKind::MemoryRead || kind == TlpKind::MemoryWrite ||
           isConfig();
}

/** 是否为配置读写请求。 */
bool
Tlp::isConfig() const
{
    return kind == TlpKind::ConfigRead || kind == TlpKind::ConfigWrite;
}

/** 请求首个使能字节相对 DWORD 地址的偏移。 */
unsigned
Tlp::firstOffset() const
{
    if (!firstBe) {
        return 0;
    }
    return __builtin_ctz(static_cast<unsigned>(firstBe));
}

/** 请求从首个到末个使能字节的跨度，含允许的中间空洞。 */
unsigned
Tlp::requestBytes() const
{
    unsigned final_be = length == 1 ? firstBe : lastBe;
    if (!final_be) {
        return 4;
    }
    unsigned last = 31 - __builtin_clz(final_be);
    return (length - 1) * 4 + last + 1 - firstOffset();
}

/** 请求中相对 DWORD 地址的 byte 是否使能。 */
bool
Tlp::byteEnabled(unsigned byte) const
{
    if (byte >= length * 4) {
        return false;
    }
    if (byte < 4) {
        return firstBe & (1u << byte);
    }
    if (byte >= (length - 1) * 4) {
        return lastBe & (1u << (byte & 3));
    }
    return true;
}

/** TLP 头部字节数。 */
unsigned
Tlp::headerBytes() const
{
    if (kind == TlpKind::Message) {
        return 16;
    }
    if (kind == TlpKind::Unsupported) {
        return (unsupportedFmt & 1) ? 16 : 12;
    }
    if ((kind == TlpKind::MemoryRead || kind == TlpKind::MemoryWrite) &&
        address > UINT32_MAX) {
        return 16;
    }
    return 12;
}

/** 按标准线上字段生成头与载荷，序号和 CRC 由数据链路层添加。 */
std::vector<uint8_t>
Tlp::encode() const
{
    unsigned format = headerBytes() == 16 ? 1 : 0;
    if (!payload.empty()) {
        format |= 2;
    }
    unsigned type = 0;
    if (isConfig()) {
        type = 4;
    }
    if (kind == TlpKind::Completion) {
        type = 10;
    }
    if (kind == TlpKind::Message) {
        type = 16 | messageRouting;
    }
    if (kind == TlpKind::Unsupported) {
        type = unsupportedType;
        format = unsupportedFmt;
    }
    unsigned wire_length = length & 1023;
    if ((kind == TlpKind::Completion || kind == TlpKind::Message) &&
        payload.empty()) {
        wire_length = 0;
    }
    uint32_t dw0 = (format << 29) | (type << 24) | (tc << 20) |
                   ((attributes & 4) << 16) | (poisoned << 14) |
                   ((attributes & 3) << 12) | wire_length;
    std::vector<uint8_t> bytes;
    appendDw(bytes, dw0);
    if (kind == TlpKind::Completion) {
        appendDw(bytes, (uint32_t(completer) << 16) |
                            (unsigned(status) << 13) | (bcm << 12) |
                            (byteCount & 4095));
        appendDw(bytes,
                 (uint32_t(requester) << 16) | (tag << 8) | lowerAddress);
    } else if (kind == TlpKind::Message) {
        appendDw(bytes,
                 (uint32_t(requester) << 16) | (tag << 8) | messageCode);
        appendDw(bytes, uint32_t(destination) << 16);
        appendDw(bytes, 0);
    } else {
        appendDw(bytes, (uint32_t(requester) << 16) | (tag << 8) |
                            (lastBe << 4) | firstBe);
        if (isConfig() ||
            (kind == TlpKind::Unsupported && unsupportedType == 5)) {
            appendDw(bytes, (uint32_t(destination) << 16) | (address & 4092));
        } else {
            if (headerBytes() == 16) {
                appendDw(bytes, address >> 32);
            }
            appendDw(bytes, address & UINT32_MAX);
        }
    }
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    return bytes;
}

/** 解码已通过 LCRC 的 TLP，结构错误返回原因。 */
bool
Tlp::decode(const std::vector<uint8_t> &bytes, Tlp &tlp, std::string &error)
{
    error.clear();
    if (bytes.size() < 12) {
        error = "short TLP header";
        return false;
    }
    uint32_t dw0 = readDw(bytes, 0);
    unsigned format = dw0 >> 29;
    unsigned type = (dw0 >> 24) & 31;
    unsigned header = (format & 1) ? 16 : 12;
    bool data = format & 2;
    unsigned length = dw0 & 1023;
    if (!length) {
        length = 1024;
    }
    if (format > 3 || (dw0 & (1u << 15)) ||
        bytes.size() != header + (data ? length * 4 : 0)) {
        error = "TLP framing length or unsupported digest";
        return false;
    }
    tlp = Tlp{};
    tlp.length = length;
    tlp.tc = (dw0 >> 20) & 7;
    tlp.attributes = ((dw0 >> 16) & 4) | ((dw0 >> 12) & 3);
    tlp.poisoned = dw0 & (1u << 14);
    uint32_t dw1 = readDw(bytes, 4);
    uint32_t dw2 = readDw(bytes, 8);
    tlp.requester = dw1 >> 16;
    tlp.tag = dw1 >> 8;
    tlp.firstBe = dw1 & 15;
    tlp.lastBe = (dw1 >> 4) & 15;
    if (type == 0) {
        tlp.kind = data ? TlpKind::MemoryWrite : TlpKind::MemoryRead;
        tlp.address = dw2 & ~uint64_t(3);
        if (header == 16) {
            tlp.address = (uint64_t(dw2) << 32) | (readDw(bytes, 12) & ~3u);
        }
    } else if (type == 4 && header == 12) {
        tlp.kind = data ? TlpKind::ConfigWrite : TlpKind::ConfigRead;
        tlp.destination = dw2 >> 16;
        tlp.address = dw2 & 4092;
    } else if (type == 10 && header == 12) {
        tlp.kind = TlpKind::Completion;
        tlp.completer = dw1 >> 16;
        tlp.status = CompletionStatus((dw1 >> 13) & 7);
        tlp.bcm = dw1 & (1u << 12);
        tlp.byteCount = dw1 & 4095;
        if (!tlp.byteCount) {
            tlp.byteCount = 4096;
        }
        tlp.requester = dw2 >> 16;
        tlp.tag = dw2 >> 8;
        tlp.lowerAddress = dw2 & 127;
    } else if ((type & 24) == 16 && header == 16) {
        tlp.kind = TlpKind::Message;
        tlp.messageRouting = type & 7;
        tlp.messageCode = dw1 & 255;
        tlp.destination = dw2 >> 16;
    } else {
        tlp.kind = TlpKind::Unsupported;
        tlp.unsupportedFmt = format;
        tlp.unsupportedType = type;
        tlp.address = header == 16
                          ? (uint64_t(dw2) << 32) | (readDw(bytes, 12) & ~3u)
                          : dw2 & ~3u;
        if (type == 5) {
            tlp.destination = dw2 >> 16;
            tlp.address = dw2 & 4092;
        }
    }
    if (data) {
        tlp.payload.assign(bytes.begin() + header, bytes.end());
    }
    return true;
}

/** 检查事务层格式、Byte Enable、MPS 和 MRRS，成功返回空字符串。 */
std::string
Tlp::validate(unsigned mps, unsigned mrrs) const
{
    if (kind == TlpKind::Unsupported && unsupportedType != 1 &&
        unsupportedType != 2 && unsupportedType != 5 &&
        unsupportedType != 12 && unsupportedType != 13 &&
        unsupportedType != 14) {
        return "reserved TLP type";
    }
    if (tc > 7 || attributes > 7 || length > 1024 ||
        (!length && (isRequest() || !payload.empty()))) {
        return "invalid VC0 fields";
    }
    if (payload.size() > mps ||
        (!payload.empty() && payload.size() != length * 4)) {
        return "payload length or MPS";
    }
    if (isRequest()) {
        if ((address & 3) || (address & 4095) + length * 4 > 4096) {
            return "address alignment or 4 KiB boundary";
        }
        if (length == 1 && lastBe) {
            return "single DWORD Last BE";
        }
        if (length > 1 && (!firstBe || !lastBe)) {
            return "multi DWORD BE";
        }
        if ((length > 2 || (address & 7)) && length > 1) {
            if ((firstBe | (firstBe - 1)) != 15 ||
                (lastBe & (lastBe + 1)) != 0) {
                return "noncontiguous multi DWORD BE";
            }
        }
        if (kind == TlpKind::MemoryRead && length * 4 > mrrs) {
            return "MRRS";
        }
        bool write =
            kind == TlpKind::MemoryWrite || kind == TlpKind::ConfigWrite;
        if (write != !payload.empty()) {
            return "request format";
        }
        if (isConfig() && (length != 1 || attributes || tc)) {
            return "configuration fields";
        }
    }
    if (kind == TlpKind::Completion) {
        if (unsigned(status) != 0 && unsigned(status) != 1 &&
            unsigned(status) != 2 && unsigned(status) != 4) {
            return "reserved completion status";
        }
        if (status != CompletionStatus::Success && !payload.empty()) {
            return "failed completion with payload";
        }
    }
    return {};
}

/** 构造连续字节范围的读写请求，复制写载荷。 */
Tlp
Tlp::request(TlpKind kind, uint64_t address, unsigned bytes,
             uint16_t requester, uint8_t tag, const uint8_t *data)
{
    if (!bytes || bytes > 4096 || (address & 4095) + bytes > 4096) {
        throw std::invalid_argument("PCIe request range");
    }
    Tlp tlp;
    tlp.kind = kind;
    tlp.address = address & ~uint64_t(3);
    tlp.requester = requester;
    tlp.tag = tag;
    unsigned offset = address & 3;
    tlp.length = (offset + bytes + 3) / 4;
    tlp.firstBe = 15 & (15 << offset);
    tlp.lastBe = 15 >> ((4 - ((offset + bytes) & 3)) & 3);
    if (tlp.length == 1) {
        tlp.firstBe &= tlp.lastBe;
        tlp.lastBe = 0;
    }
    if (kind == TlpKind::MemoryWrite || kind == TlpKind::ConfigWrite) {
        if (!data) {
            throw std::invalid_argument("PCIe write without payload");
        }
        tlp.payload.resize(tlp.length * 4, 0);
        std::copy(data, data + bytes, tlp.payload.begin() + offset);
    }
    return tlp;
}

/** 形成一个完成片段，data 只含本片段实际返回的请求字节。 */
Tlp
Tlp::completion(const Tlp &request, uint16_t completer, unsigned offset,
                const std::vector<uint8_t> &data, CompletionStatus status)
{
    if (offset > request.requestBytes() ||
        data.size() > request.requestBytes() - offset) {
        throw std::invalid_argument("PCIe completion range");
    }
    Tlp tlp;
    tlp.kind = TlpKind::Completion;
    tlp.requester = request.requester;
    tlp.completer = completer;
    tlp.tag = request.tag;
    tlp.tc = request.tc;
    tlp.attributes = request.attributes & 3;
    tlp.status = status;
    tlp.byteCount = request.isConfig() ? 4 : request.requestBytes() - offset;
    if (!request.isConfig() && request.length == 1 && !request.firstBe) {
        tlp.byteCount = 1;
    }
    uint64_t address = request.address + request.firstOffset() + offset;
    tlp.lowerAddress = request.isConfig() ? 0 : address & 127;
    if (status == CompletionStatus::Success && !data.empty()) {
        unsigned leading = address & 3;
        tlp.length = (leading + data.size() + 3) / 4;
        tlp.payload.resize(tlp.length * 4, 0);
        std::copy(data.begin(), data.end(), tlp.payload.begin() + leading);
    }
    return tlp;
}

/** 按标准多项式、初值与结果映射计算 PCIe CRC 的线上字节。 */
std::vector<uint8_t>
crc(const std::vector<uint8_t> &bytes, unsigned width, uint32_t polynomial)
{
    if (width != 16 && width != 32) {
        throw std::invalid_argument("PCIe CRC width");
    }
    uint32_t mask = width == 32 ? UINT32_MAX : 65535;
    uint32_t state = mask;
    for (uint8_t byte : bytes) {
        for (unsigned bit = 0; bit < 8; ++bit) {
            bool feedback = ((state >> (width - 1)) ^ (byte >> bit)) & 1;
            state = (state << 1) & mask;
            if (feedback) {
                state ^= polynomial;
            }
        }
    }
    state = ~state & mask;
    std::vector<uint8_t> result(width / 8, 0);
    // 原文图 3-14 的 CRC 字段高位字节先发送，表 3-5/6 在每字节内反转。
    for (unsigned bit = 0; bit < width; ++bit) {
        result[width / 8 - 1 - bit / 8] |= ((state >> bit) & 1)
                                           << (7 - bit % 8);
    }
    return result;
}

/** 添加 16 位 CRC 并返回六字节 DLLP。 */
std::vector<uint8_t>
Dllp::encode() const
{
    std::vector<uint8_t> bytes{type, data[0], data[1], data[2]};
    auto check = crc(bytes, 16, pciedefs::DLLP_CRC_POLYNOMIAL);
    bytes.insert(bytes.end(), check.begin(), check.end());
    return bytes;
}

/** 检查六字节 DLLP 的 CRC 并解码。 */
bool
Dllp::decode(const std::vector<uint8_t> &bytes, Dllp &dllp)
{
    if (bytes.size() != 6) {
        return false;
    }
    std::vector<uint8_t> body(bytes.begin(), bytes.begin() + 4);
    auto check = crc(body, 16, pciedefs::DLLP_CRC_POLYNOMIAL);
    if (!std::equal(check.begin(), check.end(), bytes.begin() + 4)) {
        return false;
    }
    dllp.type = bytes[0];
    std::copy_n(bytes.begin() + 1, 3, dllp.data.begin());
    return true;
}

/** 构造累计 ACK 或 NAK。 */
Dllp
Dllp::acknowledge(uint16_t sequence, bool nak)
{
    Dllp dllp;
    dllp.type = nak ? 16 : 0;
    dllp.data[1] = (sequence >> 8) & 15;
    dllp.data[2] = sequence & 255;
    return dllp;
}

/** 读取 ACK/NAK 的十二位序号。 */
uint16_t
Dllp::sequence() const
{
    return ((data[1] & 15) << 8) | data[2];
}

/** 构造 FC DLLP，scale 编码 0/1/2/3 对应未协商/1/4/16。 */
Dllp
Dllp::flowControl(uint8_t type, unsigned header, unsigned data,
                  unsigned header_scale, unsigned data_scale)
{
    Dllp dllp;
    dllp.type = type;
    dllp.data[0] = ((header_scale & 3) << 6) | ((header >> 2) & 63);
    dllp.data[1] =
        ((header & 3) << 6) | ((data_scale & 3) << 4) | ((data >> 8) & 15);
    dllp.data[2] = data & 255;
    return dllp;
}

/** 读取八位线上 HdrFC。 */
unsigned
Dllp::headerCredit() const
{
    return ((data[0] & 63) << 2) | (data[1] >> 6);
}

/** 读取十二位线上 DataFC。 */
unsigned
Dllp::dataCredit() const
{
    return ((data[1] & 15) << 8) | data[2];
}

/** 读取 HdrScale 编码。 */
unsigned
Dllp::headerScale() const
{
    return data[0] >> 6;
}

/** 读取 DataScale 编码。 */
unsigned
Dllp::dataScale() const
{
    return (data[1] >> 4) & 3;
}

/** 形成保留位、序号、TLP 与 LCRC 组成的 replay 副本。 */
std::vector<uint8_t>
dataLinkPacket(const Tlp &tlp, uint16_t sequence)
{
    std::vector<uint8_t> bytes{uint8_t((sequence >> 8) & 15),
                               uint8_t(sequence & 255)};
    auto body = tlp.encode();
    bytes.insert(bytes.end(), body.begin(), body.end());
    auto check = crc(bytes, 32, pciedefs::LCRC_POLYNOMIAL);
    bytes.insert(bytes.end(), check.begin(), check.end());
    return bytes;
}

/** 验证 replay 副本并取出序号、TLP 原始字节。 */
bool
checkDataLinkPacket(const std::vector<uint8_t> &bytes, uint16_t &sequence,
                    std::vector<uint8_t> &tlp)
{
    if (bytes.size() < 18) {
        return false;
    }
    std::vector<uint8_t> body(bytes.begin(), bytes.end() - 4);
    auto check = crc(body, 32, pciedefs::LCRC_POLYNOMIAL);
    if (!std::equal(check.begin(), check.end(), bytes.end() - 4)) {
        return false;
    }
    sequence = ((bytes[0] & 15) << 8) | bytes[1];
    tlp.assign(bytes.begin() + 2, bytes.end() - 4);
    return true;
}

/** 生成 Gen3/4 的四字节 STP Token。 */
std::array<uint8_t, 4>
startToken(unsigned length_dw, uint16_t sequence)
{
    if (length_dw < 5 || length_dw >= 1152 || sequence >= 4096) {
        throw std::invalid_argument("PCIe STP fields");
    }
    constexpr unsigned masks[] = {
        pciedefs::FRAME_CRC_MASK0,
        pciedefs::FRAME_CRC_MASK1,
        pciedefs::FRAME_CRC_MASK2,
        pciedefs::FRAME_CRC_MASK3,
    };
    unsigned frame_crc = 0;
    for (unsigned bit = 0; bit < 4; ++bit) {
        frame_crc |= parity(length_dw & masks[bit]) << bit;
    }
    return {uint8_t(((length_dw & 15) << 4) | 15),
            uint8_t((parity(length_dw & pciedefs::FRAME_PARITY_MASK) << 7) |
                    (length_dw >> 4)),
            uint8_t((frame_crc << 4) | (sequence >> 8)), uint8_t(sequence)};
}

/** 验证 Gen3/4 STP 的 Frame CRC、Parity、长度与序号。 */
bool
checkStartToken(const std::array<uint8_t, 4> &token, unsigned &length_dw,
                uint16_t &sequence)
{
    length_dw = (token[0] >> 4) | ((token[1] & 127) << 4);
    sequence = ((token[2] & 15) << 8) | token[3];
    if (length_dw < 5 || length_dw >= 1152) {
        return false;
    }
    return token == startToken(length_dw, sequence);
}

/** 判断 younger 是否可越过同 TC 的 older，包含可选 RO/IDO 许可。 */
bool
mayPass(const Tlp &younger, const Tlp &older)
{
    if (younger.tc != older.tc) {
        return true;
    }
    auto young = younger.traffic();
    auto old = older.traffic();
    if (old == Traffic::Posted) {
        bool relaxed = younger.attributes & 2;
        if (younger.kind == TlpKind::MemoryRead || younger.isConfig()) {
            relaxed = false;
        }
        bool distinct_id = (younger.attributes & 4) &&
                           (younger.kind == TlpKind::Completion
                                ? younger.completer
                                : younger.requester) != older.requester;
        return relaxed || distinct_id;
    }
    if (young == Traffic::Completion && old == Traffic::Completion) {
        return younger.requester != older.requester ||
               younger.tag != older.tag;
    }
    return true;
}

} // namespace gem5::gw100::pcie
