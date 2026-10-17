#include "acis/numerics.hh"

#include <cmath>
#include <cstring>
#include <limits>

namespace acis
{
namespace fc
{

namespace
{

uint32_t
bitsOf(float f)
{
    uint32_t u;
    std::memcpy(&u, &f, sizeof(u));
    return u;
}

float
floatOf(uint32_t u)
{
    float f;
    std::memcpy(&f, &u, sizeof(f));
    return f;
}

int64_t
saturate(double v, int64_t lo, int64_t hi)
{
    if (std::isnan(v))
        return 0;
    double r = std::nearbyint(v);
    if (r < static_cast<double>(lo))
        return lo;
    if (r > static_cast<double>(hi))
        return hi;
    return static_cast<int64_t>(r);
}

} // anonymous namespace

uint16_t
f32ToF16(float f)
{
    uint32_t x = bitsOf(f);
    uint16_t sign = (x >> 16) & 0x8000;
    uint32_t exp = (x >> 23) & 0xff;
    uint32_t mant = x & 0x7fffff;

    if (exp == 0xff)  // Inf 或 NaN
        return sign | 0x7c00 | (mant ? 0x200 : 0);

    int32_t e = static_cast<int32_t>(exp) - 127 + 15;
    if (e >= 0x1f)  // 上溢
        return sign | 0x7c00;

    if (e <= 0) {  // 半精度的非规格化数或下溢到 0
        if (e < -10)
            return sign;
        mant |= 0x800000;
        uint32_t shift = static_cast<uint32_t>(14 - e);
        uint32_t half = mant >> shift;
        uint32_t rem = mant & ((1u << shift) - 1);
        uint32_t halfway = 1u << (shift - 1);
        if (rem > halfway || (rem == halfway && (half & 1)))
            half++;
        return sign | static_cast<uint16_t>(half);
    }

    uint32_t half = (static_cast<uint32_t>(e) << 10) | (mant >> 13);
    uint32_t rem = mant & 0x1fff;
    if (rem > 0x1000 || (rem == 0x1000 && (half & 1)))
        half++;  // 进位可能把指数推到 0x1f，正好得到 Inf
    return sign | static_cast<uint16_t>(half);
}

float
f16ToF32(uint16_t h)
{
    uint32_t sign = static_cast<uint32_t>(h & 0x8000) << 16;
    uint32_t exp = (h >> 10) & 0x1f;
    uint32_t mant = h & 0x3ff;

    if (exp == 0) {
        if (mant == 0)
            return floatOf(sign);
        // 非规格化数：规格化后再拼成单精度
        int e = -1;
        do {
            mant <<= 1;
            e++;
        } while (!(mant & 0x400));
        mant &= 0x3ff;
        return floatOf(sign | static_cast<uint32_t>(127 - 15 - e) << 23 | mant << 13);
    }
    if (exp == 0x1f)
        return floatOf(sign | 0x7f800000 | (mant << 13));
    return floatOf(sign | (exp - 15 + 127) << 23 | mant << 13);
}

uint16_t
f32ToBf16(float f)
{
    uint32_t x = bitsOf(f);
    if (std::isnan(f))
        return static_cast<uint16_t>((x >> 16) | 0x40);  // 保持 quiet NaN
    uint32_t lsb = (x >> 16) & 1;
    return static_cast<uint16_t>((x + 0x7fff + lsb) >> 16);
}

float
bf16ToF32(uint16_t b)
{
    return floatOf(static_cast<uint32_t>(b) << 16);
}

bool
isFloat(isa::DType t)
{
    return t == isa::DType::FP16 || t == isa::DType::BF16 || t == isa::DType::FP32;
}

unsigned
elemBytes(isa::DType t)
{
    return isa::dtypeBits(t) / 8;
}

size_t
denseBytes(isa::DType t, size_t n)
{
    return (n * isa::dtypeBits(t) + 7) / 8;
}

double
loadElem(const uint8_t *buf, isa::DType t, size_t i)
{
    switch (t) {
      case isa::DType::INT8:
        return static_cast<int8_t>(buf[i]);
      case isa::DType::UINT8:
        return buf[i];
      case isa::DType::INT16: {
        int16_t v;
        std::memcpy(&v, buf + 2 * i, 2);
        return v;
      }
      case isa::DType::INT32: {
        int32_t v;
        std::memcpy(&v, buf + 4 * i, 4);
        return v;
      }
      case isa::DType::FP16: {
        uint16_t v;
        std::memcpy(&v, buf + 2 * i, 2);
        return f16ToF32(v);
      }
      case isa::DType::BF16: {
        uint16_t v;
        std::memcpy(&v, buf + 2 * i, 2);
        return bf16ToF32(v);
      }
      case isa::DType::FP32: {
        float v;
        std::memcpy(&v, buf + 4 * i, 4);
        return v;
      }
      case isa::DType::INT4: {
        uint8_t byte = buf[i / 2];
        int nib = (i % 2) ? (byte >> 4) : (byte & 0xf);
        return nib >= 8 ? nib - 16 : nib;
      }
    }
    return 0.0;
}

void
storeElem(uint8_t *buf, isa::DType t, size_t i, double v)
{
    switch (t) {
      case isa::DType::INT8:
        buf[i] = static_cast<uint8_t>(static_cast<int8_t>(saturate(v, -128, 127)));
        return;
      case isa::DType::UINT8:
        buf[i] = static_cast<uint8_t>(saturate(v, 0, 255));
        return;
      case isa::DType::INT16: {
        int16_t x = static_cast<int16_t>(saturate(v, -32768, 32767));
        std::memcpy(buf + 2 * i, &x, 2);
        return;
      }
      case isa::DType::INT32: {
        int32_t x = static_cast<int32_t>(saturate(
            v, std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::max()));
        std::memcpy(buf + 4 * i, &x, 4);
        return;
      }
      case isa::DType::FP16: {
        uint16_t x = f32ToF16(static_cast<float>(v));
        std::memcpy(buf + 2 * i, &x, 2);
        return;
      }
      case isa::DType::BF16: {
        uint16_t x = f32ToBf16(static_cast<float>(v));
        std::memcpy(buf + 2 * i, &x, 2);
        return;
      }
      case isa::DType::FP32: {
        float x = static_cast<float>(v);
        std::memcpy(buf + 4 * i, &x, 4);
        return;
      }
      case isa::DType::INT4: {
        uint8_t nib = static_cast<uint8_t>(saturate(v, -8, 7)) & 0xf;
        uint8_t &byte = buf[i / 2];
        byte = (i % 2) ? static_cast<uint8_t>((byte & 0x0f) | (nib << 4))
                       : static_cast<uint8_t>((byte & 0xf0) | nib);
        return;
      }
    }
}

} // namespace fc
} // namespace acis
