/**
 * @file  capi.cc
 * @brief 供 L0 测试经 ctypes 调用的 C 接口
 *
 * 用 numpy 写的 golden 与这里的 C++ 实现逐位比对。gem5 模型不使用这些函数。
 */

#include <cstring>
#include <string>

#include "acis/inst.hh"
#include "acis/numerics.hh"
#include "acis/vector_ops.hh"

using namespace acis;

extern "C" {

uint16_t acis_f32_to_f16(float f) { return fc::f32ToF16(f); }
float acis_f16_to_f32(uint16_t h) { return fc::f16ToF32(h); }
uint16_t acis_f32_to_bf16(float f) { return fc::f32ToBf16(f); }
float acis_bf16_to_f32(uint16_t b) { return fc::bf16ToF32(b); }

int
acis_vec_binary(int op, int ta, const void *a, int tb, const void *b,
                int tc, void *c, size_t n)
{
    return fc::vecBinary(static_cast<isa::VecBinaryOp>(op),
                         static_cast<isa::DType>(ta), static_cast<const uint8_t *>(a),
                         static_cast<isa::DType>(tb), static_cast<const uint8_t *>(b),
                         static_cast<isa::DType>(tc), static_cast<uint8_t *>(c), n) ? 1 : 0;
}

int
acis_vec_scalar(int op, int ta, const void *a, float imm, int tc, void *c, size_t n)
{
    return fc::vecScalar(static_cast<isa::VecScalarOp>(op),
                         static_cast<isa::DType>(ta), static_cast<const uint8_t *>(a), imm,
                         static_cast<isa::DType>(tc), static_cast<uint8_t *>(c), n) ? 1 : 0;
}

/** 译码再编码，检查编解码是否互逆；失败时把原因写进 err。 */
int
acis_decode_roundtrip(const uint8_t *in, uint8_t *out, char *err, size_t err_len)
{
    TensorInst inst;
    std::string msg;
    if (!decode(in, inst, msg)) {
        std::strncpy(err, msg.c_str(), err_len - 1);
        err[err_len - 1] = '\0';
        return 0;
    }
    encode(inst, out);
    return 1;
}

/** 列出指令第 k 个操作数的字节区间；返回区间个数（超过 max 时只写前 max 个）。 */
size_t
acis_byte_runs(const uint8_t *inst_bytes, int k, uint64_t *addrs, uint32_t *lens, size_t max)
{
    TensorInst inst;
    std::string msg;
    if (!decode(inst_bytes, inst, msg))
        return 0;
    auto runs = byteRuns(inst.opnd[k], inst.dtype[k]);
    for (size_t i = 0; i < runs.size() && i < max; ++i) {
        addrs[i] = runs[i].addr;
        lens[i] = runs[i].bytes;
    }
    return runs.size();
}

/** 指令第 k 个操作数的地址范围 [lo, hi)。 */
int
acis_addr_span(const uint8_t *inst_bytes, int k, uint64_t *lo, uint64_t *hi)
{
    TensorInst inst;
    std::string msg;
    if (!decode(inst_bytes, inst, msg))
        return 0;
    const AddrSpan s = addrSpan(inst.opnd[k], inst.dtype[k]);
    *lo = s.lo;
    *hi = s.hi;
    return 1;
}

/** 按本地窗口换算后重新编码；失败时把原因写进 err。 */
int
acis_resolve_local(const uint8_t *in, uint8_t *out, uint64_t win_base, uint64_t win_bytes,
                   uint64_t sram_base, uint64_t sram_bytes, char *err, size_t err_len)
{
    TensorInst inst;
    std::string msg;
    if (!decode(in, inst, msg) ||
        !resolveLocal(inst, {win_base, win_bytes, sram_base, sram_bytes}, msg)) {
        std::strncpy(err, msg.c_str(), err_len - 1);
        err[err_len - 1] = '\0';
        return 0;
    }
    encode(inst, out);
    return 1;
}

} // extern "C"
