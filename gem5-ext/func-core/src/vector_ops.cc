/**
 * @file  vector_ops.cc
 * @brief vector_ops.hh 的实现
 *
 * @see SPEC-220
 */

#include "acis/vector_ops.hh"

#include <cmath>
#include <limits>

#include "acis/numerics.hh"

namespace acis
{
namespace fc
{

namespace
{

/** max / min 规定：任一输入为 NaN 时结果为 NaN（与 numpy.maximum 一致）。 */
double
nanMax(double x, double y)
{
    if (std::isnan(x) || std::isnan(y))
        return std::numeric_limits<double>::quiet_NaN();
    return x > y ? x : y;
}

double
nanMin(double x, double y)
{
    if (std::isnan(x) || std::isnan(y))
        return std::numeric_limits<double>::quiet_NaN();
    return x < y ? x : y;
}

/** 浮点路径先把两个输入舍入到 FP32，结果再按 FP32 运算的语义舍入一次。 */
double
asF32(double v)
{
    return static_cast<double>(static_cast<float>(v));
}

} // anonymous namespace

bool
vecBinary(isa::VecBinaryOp op,
          isa::DType ta, const uint8_t *a,
          isa::DType tb, const uint8_t *b,
          isa::DType tc, uint8_t *c, size_t n)
{
    const bool fp = isFloat(ta) || isFloat(tb) || isFloat(tc);
    for (size_t i = 0; i < n; ++i) {
        double x = loadElem(a, ta, i);
        double y = loadElem(b, tb, i);
        if (fp) {
            x = asF32(x);
            y = asF32(y);
        }
        double r;
        switch (op) {
          case isa::VecBinaryOp::ADD: r = x + y; break;
          case isa::VecBinaryOp::SUB: r = x - y; break;
          case isa::VecBinaryOp::MUL: r = x * y; break;
          case isa::VecBinaryOp::MAX: r = nanMax(x, y); break;
          case isa::VecBinaryOp::MIN: r = nanMin(x, y); break;
          default: return false;
        }
        storeElem(c, tc, i, fp ? asF32(r) : r);
    }
    return true;
}

bool
vecScalar(isa::VecScalarOp op,
          isa::DType ta, const uint8_t *a, float imm,
          isa::DType tc, uint8_t *c, size_t n)
{
    const bool fp = isFloat(ta) || isFloat(tc);
    const double y = imm;
    for (size_t i = 0; i < n; ++i) {
        double x = loadElem(a, ta, i);
        if (fp)
            x = asF32(x);
        double r;
        switch (op) {
          case isa::VecScalarOp::ADD: r = x + y; break;
          case isa::VecScalarOp::MUL: r = x * y; break;
          case isa::VecScalarOp::MAX: r = nanMax(x, y); break;
          default: return false;
        }
        storeElem(c, tc, i, fp ? asF32(r) : r);
    }
    return true;
}

} // namespace fc
} // namespace acis
