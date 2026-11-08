/**
 * @file  packet.hh
 * @brief PCIe TLP、DLLP 和完整性编码
 *
 * 线上字段与本地观测编号分离。编码按 BASE4-CB §2.2、§3.5、§3.6
 * 和 §4.2.2.3，数据载荷保持字节地址顺序。
 *
 * @see spec-pcie
 */

#ifndef GW100_PCIE_LINK_PACKET_HH
#define GW100_PCIE_LINK_PACKET_HH

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace gem5::gw100::pcie
{

/** 事务所属的独立流控类别。 */
enum class Traffic : unsigned
{
    /** Posted。 */
    Posted,
    /** Non-Posted。 */
    NonPosted,
    /** Completion。 */
    Completion,
};

/** 首版支持的事务类型。 */
enum class TlpKind : unsigned
{
    /** Memory Read。 */
    MemoryRead,
    /** Memory Write。 */
    MemoryWrite,
    /** Configuration Type 0 Read。 */
    ConfigRead,
    /** Configuration Type 0 Write。 */
    ConfigWrite,
    /** Completion，载荷由 payload 表示。 */
    Completion,
    /** Message，含路由子字段。 */
    Message,
    /** 可形成合法帧但事务类型未实现。 */
    Unsupported,
};

/** 完成头中的 Completion Status 编码。 */
enum class CompletionStatus : uint8_t
{
    /** 成功。 */
    Success = 0,
    /** Unsupported Request。 */
    Ur = 1,
    /** Configuration Request Retry Status。 */
    Crs = 2,
    /** Completer Abort。 */
    Ca = 4,
};

/** TLP 的标准字段，cookie 只用于本地 trace 和提交通知。 */
struct Tlp
{
    /** 事务类型。 */
    TlpKind kind = TlpKind::MemoryRead;
    /** 同一方向的本地观测编号，不进入线上编码和完成匹配。 */
    uint64_t cookie = 0;
    /** DWORD 对齐的地址或配置偏移，单位 B。 */
    uint64_t address = 0;
    /** Requester ID，Completion 仍填写原请求者。 */
    uint16_t requester = 0;
    /** Completer ID。 */
    uint16_t completer = 0;
    /** 配置或 ID 路由目的 ID。 */
    uint16_t destination = 0;
    /** 首版线上 tag，最多八位。 */
    uint8_t tag = 0;
    /** Traffic Class。 */
    uint8_t tc = 0;
    /** Attr[2:0]，依次为 IDO、RO、No Snoop。 */
    uint8_t attributes = 0;
    /** 请求或带数据完成的 DWORD 长度，1–1024。 */
    unsigned length = 1;
    /** First DWORD Byte Enable。 */
    uint8_t firstBe = 15;
    /** Last DWORD Byte Enable，单 DWORD 请求为零。 */
    uint8_t lastBe = 0;
    /** Completion Status。 */
    CompletionStatus status = CompletionStatus::Success;
    /** Completion Byte Count，解码后以 4096 表示线上零值。 */
    unsigned byteCount = 0;
    /** Completion Lower Address。 */
    uint8_t lowerAddress = 0;
    /** Byte Count Modified，首版 RC 不生成。 */
    bool bcm = false;
    /** Poisoned TLP。 */
    bool poisoned = false;
    /** Message Code。 */
    uint8_t messageCode = 0;
    /** Message 的 routing 子字段，0 到 RC，4 本地接收。 */
    uint8_t messageRouting = 0;
    /** 标准头字段以外的 DWORD 对齐数据，包含禁用字节填充。 */
    std::vector<uint8_t> payload;
    /** Unsupported 类型的原始 Fmt，保留给错误处理。 */
    uint8_t unsupportedFmt = 0;
    /** Unsupported 类型的原始 Type，保留给错误处理。 */
    uint8_t unsupportedType = 0;

    /** 返回对应的流控类别。 */
    Traffic traffic() const;
    /** 是否为携带地址与 Byte Enable 的请求。 */
    bool isRequest() const;
    /** 是否为配置读写请求。 */
    bool isConfig() const;
    /** 请求首个使能字节相对 DWORD 地址的偏移。 */
    unsigned firstOffset() const;
    /** 请求从首个到末个使能字节的跨度，含允许的中间空洞。 */
    unsigned requestBytes() const;
    /** 请求中相对 DWORD 地址的 byte 是否使能。 */
    bool byteEnabled(unsigned byte) const;
    /** TLP 头部字节数。 */
    unsigned headerBytes() const;
    /** 按标准线上字段生成头与载荷，序号和 CRC 由数据链路层添加。 */
    std::vector<uint8_t> encode() const;
    /** 解码已通过 LCRC 的 TLP，结构错误返回原因。 */
    static bool decode(const std::vector<uint8_t> &bytes, Tlp &tlp,
                       std::string &error);
    /** 检查事务层格式、Byte Enable、MPS 和 MRRS，成功返回空字符串。 */
    std::string validate(unsigned mps, unsigned mrrs) const;
    /** 构造连续字节范围的读写请求，复制写载荷。 */
    static Tlp request(TlpKind kind, uint64_t address, unsigned bytes,
                       uint16_t requester, uint8_t tag,
                       const uint8_t *data = nullptr);
    /** 形成一个完成片段，data 只含本片段实际返回的请求字节。 */
    static Tlp completion(const Tlp &request, uint16_t completer,
                          unsigned offset, const std::vector<uint8_t> &data,
                          CompletionStatus status = CompletionStatus::Success);
};

/** DLLP 的四字节内容，线上 CRC 由 encode 添加。 */
struct Dllp
{
    /** 标准 Type。 */
    uint8_t type = 0;
    /** Type 后三个字节，按线上顺序。 */
    std::array<uint8_t, 3> data{};
    /** 添加 16 位 CRC 并返回六字节 DLLP。 */
    std::vector<uint8_t> encode() const;
    /** 检查六字节 DLLP 的 CRC 并解码。 */
    static bool decode(const std::vector<uint8_t> &bytes, Dllp &dllp);
    /** 构造累计 ACK 或 NAK。 */
    static Dllp acknowledge(uint16_t sequence, bool nak = false);
    /** 读取 ACK/NAK 的十二位序号。 */
    uint16_t sequence() const;
    /** 构造 FC DLLP，scale 编码 0/1/2/3 对应未协商/1/4/16。 */
    static Dllp flowControl(uint8_t type, unsigned header, unsigned data,
                            unsigned header_scale = 1,
                            unsigned data_scale = 1);
    /** 读取八位线上 HdrFC。 */
    unsigned headerCredit() const;
    /** 读取十二位线上 DataFC。 */
    unsigned dataCredit() const;
    /** 读取 HdrScale 编码。 */
    unsigned headerScale() const;
    /** 读取 DataScale 编码。 */
    unsigned dataScale() const;
};

/** 按标准多项式、初值与结果映射计算 PCIe CRC 的线上字节。 */
std::vector<uint8_t> crc(const std::vector<uint8_t> &bytes, unsigned width,
                         uint32_t polynomial);
/** 形成保留位、序号、TLP 与 LCRC 组成的 replay 副本。 */
std::vector<uint8_t> dataLinkPacket(const Tlp &tlp, uint16_t sequence);
/** 验证 replay 副本并取出序号、TLP 原始字节。 */
bool checkDataLinkPacket(const std::vector<uint8_t> &bytes, uint16_t &sequence,
                         std::vector<uint8_t> &tlp);
/** 生成 Gen3/4 的四字节 STP Token。 */
std::array<uint8_t, 4> startToken(unsigned length_dw, uint16_t sequence);
/** 验证 Gen3/4 STP 的 Frame CRC、Parity、长度与序号。 */
bool checkStartToken(const std::array<uint8_t, 4> &token, unsigned &length_dw,
                     uint16_t &sequence);
/** 判断 younger 是否可越过同 TC 的 older，包含可选 RO/IDO 许可。 */
bool mayPass(const Tlp &younger, const Tlp &older);

} // namespace gem5::gw100::pcie
#endif
