/**
 * 数值规格 v0（SPEC-240 草案）的功能实现。
 *
 * - 浮点类型（FP16/BF16/FP32）先转成 FP32 计算，结果按"就近舍入、平局取偶"转回目的类型；
 *   上溢得到 Inf，NaN 保持为 quiet NaN。
 * - 整数类型按 int64 计算，结果饱和到目的类型的表示范围。
 * - INT4 两个元素打包在一个字节里，低半字节在前，有符号（-8..7）。
 */

#ifndef __ACIS_FC_NUMERICS_HH__
#define __ACIS_FC_NUMERICS_HH__

#include <cstddef>
#include <cstdint>

#include "acis/isa_defs.hh"

namespace acis
{
namespace fc
{

uint16_t f32ToF16(float f);
float f16ToF32(uint16_t h);
uint16_t f32ToBf16(float f);
float bf16ToF32(uint16_t b);

bool isFloat(isa::DType t);

/** 元素占用的字节数；INT4 返回 0（需按半字节处理）。 */
unsigned elemBytes(isa::DType t);

/** 从紧密排列的缓冲区读第 i 个元素。 */
double loadElem(const uint8_t *buf, isa::DType t, size_t i);

/** 向紧密排列的缓冲区写第 i 个元素，按上面的舍入和饱和规则转换。 */
void storeElem(uint8_t *buf, isa::DType t, size_t i, double v);

/** 存放 n 个元素所需的字节数。 */
size_t denseBytes(isa::DType t, size_t n);

} // namespace fc
} // namespace acis

#endif // __ACIS_FC_NUMERICS_HH__
