/**
 * 向量引擎指令的功能语义，作用在紧密排列的缓冲区上。
 * 广播和步长由调用方在读数据时展开（见 acis::byteRuns），这里只做逐元素运算。
 */

#ifndef __ACIS_FC_VECTOR_OPS_HH__
#define __ACIS_FC_VECTOR_OPS_HH__

#include <cstddef>
#include <cstdint>

#include "acis/isa_defs.hh"

namespace acis
{
namespace fc
{

/** c[i] = a[i] (op) b[i]。返回 false 表示 subop 不支持。 */
bool vecBinary(isa::VecBinaryOp op,
               isa::DType ta, const uint8_t *a,
               isa::DType tb, const uint8_t *b,
               isa::DType tc, uint8_t *c, size_t n);

/** c[i] = a[i] (op) imm。 */
bool vecScalar(isa::VecScalarOp op,
               isa::DType ta, const uint8_t *a, float imm,
               isa::DType tc, uint8_t *c, size_t n);

} // namespace fc
} // namespace acis

#endif // __ACIS_FC_VECTOR_OPS_HH__
