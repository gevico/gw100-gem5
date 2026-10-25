/**
 * @file  inst.hh
 * @brief 张量指令的编解码、操作数地址展开与 Tile 相对地址换算
 *
 * 编码格式以生成的 isa_defs.hh 为准；单一真源待重建。
 *
 * @see SPEC-110, SPEC-230
 */

#ifndef __GW100_FC_INST_HH__
#define __GW100_FC_INST_HH__

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "gw100/isa_defs.hh"

namespace gw100
{

/** 张量操作数：base 地址 + 4 维 (size, stride)，d0 为最内层，stride 单位为字节。 */
struct TensorDesc
{
    uint64_t addr = 0;
    std::array<uint16_t, 4> size{{1, 1, 1, 1}};
    std::array<int32_t, 4> stride{{0, 0, 0, 0}};

    uint64_t numElems() const;
};

/** 一段连续的字节区间。 */
struct ByteRun
{
    uint64_t addr;
    uint32_t bytes;
};

/**
 * 按逻辑顺序（d0 最快）列出张量占用的字节区间，相邻且连续的区间会合并。
 * stride 为 0 的维度表示广播，同一区间会重复出现。
 * INT4 只支持 d0 方向紧密排列，d0 的 stride 被忽略。
 */
std::vector<ByteRun> byteRuns(const TensorDesc &t, isa::DType dtype);

/** 张量占用的地址范围 [lo, hi)，考虑了广播和负 stride。 */
struct AddrSpan
{
    uint64_t lo;
    uint64_t hi;
};

AddrSpan addrSpan(const TensorDesc &t, isa::DType dtype);

struct TensorInst
{
    isa::Opcode opcode = isa::Opcode::NOP;
    uint8_t subop = 0;
    uint8_t flags = 0;
    std::array<isa::DType, 3> dtype{{isa::DType::FP32, isa::DType::FP32, isa::DType::FP32}};

    bool waitEn = false;
    uint8_t waitCnt = 0;
    uint16_t waitVal = 0;
    bool sigEn = false;
    uint8_t sigCnt = 0;
    uint16_t sigTile = isa::kLocalTile;

    float imm = 0.0f;
    uint16_t groupSize = 0;
    uint64_t auxAddr = 0;

    std::array<TensorDesc, 3> opnd;

    isa::Unit unit() const { return isa::unitOf(opcode); }
    std::string toString() const;
};

/**
 * 从 128 字节编码译码。编码非法（未知操作码或数据类型）时返回 false，
 * 并在 err 中写明原因。
 */
bool decode(const uint8_t *bytes, TensorInst &inst, std::string &err);

/** 编码为 128 字节；主要供测试使用。 */
void encode(const TensorInst &inst, uint8_t *bytes);

/**
 * Tile 相对寻址（SPEC-110 / 230）：指令中落在本地窗口 [winBase, winBase + winBytes)
 * 内的地址指本 Tile 的 SRAM，窗口内偏移即 SRAM 内偏移。这样同一条指令可以多播给
 * 多个 Tile，各自访问自己的 SRAM。其他 Tile 的 SRAM 仍用全局地址访问。
 */
struct LocalWindow
{
    uint64_t winBase;
    uint64_t winBytes;
    uint64_t sramBase;   ///< 本 Tile SRAM 的全局地址
    uint64_t sramBytes;
};

/**
 * 把 inst 中落在本地窗口内的操作数地址和 auxAddr 换算成本 Tile SRAM 的全局地址。
 * 操作数越出本 Tile SRAM 时返回 false，并在 err 中写明原因；此时 inst 可能已被部分改写。
 */
bool resolveLocal(TensorInst &inst, const LocalWindow &w, std::string &err);

const char *opcodeName(isa::Opcode op);

} // namespace gw100

#endif // __GW100_FC_INST_HH__
