// 由 tools/isagen/gen.py 根据 spec-src/isa/isa-v0.yaml 生成，请勿手改。
#ifndef __GW100_ISA_GEN_ISA_DEFS_HH__
#define __GW100_ISA_GEN_ISA_DEFS_HH__

#include <cstdint>

namespace gw100
{
namespace isa
{

constexpr const char *kVersion = "0.3";
constexpr unsigned kInstBytes = 128;
constexpr uint16_t kLocalTile = 0xffff;

enum class Unit : uint8_t
{
    Ctrl = 0,
    Dma = 1,
    Me = 2,
    Ve = 3,
};

enum class Opcode : uint8_t
{
    NOP = 0x00,
    END = 0x01,
    SYNC = 0x02,
    DMA_COPY = 0x10,
    MATMUL = 0x20,
    VEC_BINARY = 0x30,
    VEC_UNARY = 0x31,
    VEC_REDUCE = 0x32,
    VEC_SCALAR = 0x33,
};

constexpr bool
isValidOpcode(uint8_t code)
{
    switch (code) {
      case 0x00:
      case 0x01:
      case 0x02:
      case 0x10:
      case 0x20:
      case 0x30:
      case 0x31:
      case 0x32:
      case 0x33:
        return true;
      default:
        return false;
    }
}

constexpr Unit
unitOf(Opcode op)
{
    switch (op) {
      case Opcode::NOP: return Unit::Ctrl;
      case Opcode::END: return Unit::Ctrl;
      case Opcode::SYNC: return Unit::Ctrl;
      case Opcode::DMA_COPY: return Unit::Dma;
      case Opcode::MATMUL: return Unit::Me;
      case Opcode::VEC_BINARY: return Unit::Ve;
      case Opcode::VEC_UNARY: return Unit::Ve;
      case Opcode::VEC_REDUCE: return Unit::Ve;
      case Opcode::VEC_SCALAR: return Unit::Ve;
    }
    return Unit::Ctrl;
}

constexpr bool
isImplemented(Opcode op)
{
    switch (op) {
      case Opcode::NOP: return true;
      case Opcode::END: return true;
      case Opcode::SYNC: return true;
      case Opcode::DMA_COPY: return true;
      case Opcode::MATMUL: return false;
      case Opcode::VEC_BINARY: return true;
      case Opcode::VEC_UNARY: return false;
      case Opcode::VEC_REDUCE: return false;
      case Opcode::VEC_SCALAR: return true;
    }
    return false;
}

enum class DType : uint8_t
{
    INT8 = 0,
    UINT8 = 1,
    INT16 = 2,
    INT32 = 3,
    FP16 = 4,
    BF16 = 5,
    FP32 = 6,
    INT4 = 7,
};

/** 元素位宽；INT4 为 4。 */
constexpr unsigned
dtypeBits(DType t)
{
    switch (t) {
      case DType::INT8: return 8;
      case DType::UINT8: return 8;
      case DType::INT16: return 16;
      case DType::INT32: return 32;
      case DType::FP16: return 16;
      case DType::BF16: return 16;
      case DType::FP32: return 32;
      case DType::INT4: return 4;
    }
    return 0;
}

constexpr bool
isValidDType(uint8_t code)
{
    return code <= 7;
}

/** 头部字段的字节偏移。 */
namespace hdr
{
constexpr unsigned opcode = 0;  // u8
constexpr unsigned subop = 1;  // u8
constexpr unsigned flags = 2;  // u8
constexpr unsigned dtype_a = 3;  // u8
constexpr unsigned dtype_b = 4;  // u8
constexpr unsigned dtype_c = 5;  // u8
constexpr unsigned wait_en = 8;  // u8
constexpr unsigned wait_cnt = 9;  // u8
constexpr unsigned wait_val = 10;  // u16
constexpr unsigned sig_en = 12;  // u8
constexpr unsigned sig_cnt = 13;  // u8
constexpr unsigned sig_tile = 14;  // u16
constexpr unsigned imm = 16;  // f32
constexpr unsigned group_size = 20;  // u16
constexpr unsigned aux_addr = 24;  // u64
} // namespace hdr

/** 张量操作数描述符。 */
namespace opnd
{
constexpr unsigned count = 3;
constexpr unsigned base = 32;
constexpr unsigned bytes = 32;
constexpr unsigned dims = 4;
constexpr unsigned addr = 0;  // u64
constexpr unsigned size = 8;  // u16 x4
constexpr unsigned stride = 16;  // i32 x4
} // namespace opnd

namespace flags
{
constexpr uint8_t ACCUMULATE = 0x01;
} // namespace flags

enum class VecBinaryOp : uint8_t
{
    ADD = 0,
    SUB = 1,
    MUL = 2,
    MAX = 3,
    MIN = 4,
};

enum class VecUnaryOp : uint8_t
{
    COPY = 0,
    NEG = 1,
    EXP = 2,
    RSQRT = 3,
    RECIP = 4,
    SILU = 5,
    SQUARE = 6,
};

enum class VecReduceOp : uint8_t
{
    SUM = 0,
    MAX = 1,
};

enum class VecScalarOp : uint8_t
{
    ADD = 0,
    MUL = 1,
    MAX = 2,
};

namespace prog
{
constexpr uint32_t magic = 0x30315747;
constexpr unsigned header_bytes = 16;
constexpr unsigned entry_bytes = 136;
} // namespace prog

namespace mgmt_reg
{
constexpr unsigned CMD_ADDR = 0x00;
constexpr unsigned DOORBELL = 0x08;
constexpr unsigned STATUS = 0x0c;
constexpr unsigned VERSION = 0x10;
constexpr unsigned size = 0x20;
} // namespace mgmt_reg

namespace cmd_rec
{
constexpr unsigned bytes = 32;
constexpr unsigned program_addr = 0;  // u64
constexpr unsigned program_bytes = 8;  // u32
constexpr unsigned flags = 12;  // u32
constexpr unsigned completion_addr = 16;  // u64
constexpr unsigned tag = 24;  // u64
} // namespace cmd_rec

namespace cpl_rec
{
constexpr unsigned bytes = 32;
constexpr unsigned status = 0;  // u32
constexpr unsigned tag = 8;  // u64
constexpr unsigned start_tick = 16;  // u64
constexpr unsigned end_tick = 24;  // u64
} // namespace cpl_rec

} // namespace isa
} // namespace gw100

#endif // __GW100_ISA_GEN_ISA_DEFS_HH__
