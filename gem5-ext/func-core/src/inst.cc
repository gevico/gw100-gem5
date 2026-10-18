/**
 * @file  inst.cc
 * @brief inst.hh 的实现
 *
 * @see SPEC-110, SPEC-230
 */

#include "gw100/inst.hh"

#include <cstring>
#include <sstream>

namespace gw100
{

namespace
{

template <typename T>
T
rd(const uint8_t *p, unsigned off)
{
    T v;
    std::memcpy(&v, p + off, sizeof(T));
    return v;
}

template <typename T>
void
wr(uint8_t *p, unsigned off, T v)
{
    std::memcpy(p + off, &v, sizeof(T));
}

} // anonymous namespace

uint64_t
TensorDesc::numElems() const
{
    uint64_t n = 1;
    for (auto s : size)
        n *= s;
    return n;
}

std::vector<ByteRun>
byteRuns(const TensorDesc &t, isa::DType dtype)
{
    std::vector<ByteRun> runs;
    if (t.numElems() == 0)
        return runs;

    const unsigned bits = isa::dtypeBits(dtype);
    const unsigned eb = bits / 8;
    // d0 方向是否紧密排列；INT4 规定只能紧密排列
    const bool contig0 = (bits == 4) || t.stride[0] == static_cast<int32_t>(eb);
    const uint32_t row_bytes = static_cast<uint32_t>((uint64_t(t.size[0]) * bits + 7) / 8);

    auto push = [&runs](uint64_t addr, uint32_t bytes) {
        if (!runs.empty() && runs.back().addr + runs.back().bytes == addr)
            runs.back().bytes += bytes;
        else
            runs.push_back({addr, bytes});
    };

    for (uint32_t i3 = 0; i3 < t.size[3]; ++i3) {
        for (uint32_t i2 = 0; i2 < t.size[2]; ++i2) {
            for (uint32_t i1 = 0; i1 < t.size[1]; ++i1) {
                const uint64_t row = t.addr + int64_t(i3) * t.stride[3] +
                                     int64_t(i2) * t.stride[2] +
                                     int64_t(i1) * t.stride[1];
                if (contig0) {
                    push(row, row_bytes);
                } else {
                    for (uint32_t i0 = 0; i0 < t.size[0]; ++i0)
                        push(row + int64_t(i0) * t.stride[0], eb);
                }
            }
        }
    }
    return runs;
}

AddrSpan
addrSpan(const TensorDesc &t, isa::DType dtype)
{
    if (t.numElems() == 0)
        return {t.addr, t.addr};

    const unsigned bits = isa::dtypeBits(dtype);
    const unsigned eb = bits / 8;
    // 与 byteRuns 的规则一致：d0 紧密排列时按整行计
    const bool contig0 = (bits == 4) || t.stride[0] == static_cast<int32_t>(eb);
    int64_t neg = 0;
    int64_t pos = 0;
    for (unsigned d = contig0 ? 1 : 0; d < 4; ++d) {
        const int64_t off = int64_t(t.size[d] - 1) * t.stride[d];
        (off < 0 ? neg : pos) += off;
    }
    const uint64_t last = contig0 ? (uint64_t(t.size[0]) * bits + 7) / 8 : eb;
    return {t.addr + neg, t.addr + pos + last};
}

namespace
{

bool
inWindow(uint64_t a, const LocalWindow &w)
{
    return a >= w.winBase && a - w.winBase < w.winBytes;
}

std::string
hex(uint64_t v)
{
    std::ostringstream os;
    os << "0x" << std::hex << v;
    return os.str();
}

} // anonymous namespace

bool
resolveLocal(TensorInst &inst, const LocalWindow &w, std::string &err)
{
    for (unsigned k = 0; k < inst.opnd.size(); ++k) {
        TensorDesc &t = inst.opnd[k];
        if (!inWindow(t.addr, w))
            continue;
        const AddrSpan s = addrSpan(t, inst.dtype[k]);
        if (s.lo < w.winBase || s.hi > w.winBase + w.sramBytes) {
            err = "操作数 " + std::to_string(k) + " 越出本 Tile SRAM：本地地址 [" +
                  hex(s.lo) + ", " + hex(s.hi) + ")，SRAM 容量 " + hex(w.sramBytes);
            return false;
        }
        t.addr = t.addr - w.winBase + w.sramBase;
    }
    if (inWindow(inst.auxAddr, w)) {
        if (inst.auxAddr - w.winBase >= w.sramBytes) {
            err = "aux_addr " + hex(inst.auxAddr) + " 越出本 Tile SRAM";
            return false;
        }
        inst.auxAddr = inst.auxAddr - w.winBase + w.sramBase;
    }
    return true;
}

bool
decode(const uint8_t *b, TensorInst &inst, std::string &err)
{
    using namespace isa;
    const uint8_t op = rd<uint8_t>(b, hdr::opcode);
    if (!isValidOpcode(op)) {
        err = "未知操作码 " + std::to_string(op);
        return false;
    }
    inst.opcode = static_cast<Opcode>(op);
    inst.subop = rd<uint8_t>(b, hdr::subop);
    inst.flags = rd<uint8_t>(b, hdr::flags);
    const unsigned dt_off[3] = {hdr::dtype_a, hdr::dtype_b, hdr::dtype_c};
    for (int i = 0; i < 3; ++i) {
        const uint8_t d = rd<uint8_t>(b, dt_off[i]);
        if (!isValidDType(d)) {
            err = "操作数 " + std::to_string(i) + " 的数据类型非法：" + std::to_string(d);
            return false;
        }
        inst.dtype[i] = static_cast<DType>(d);
    }
    inst.waitEn = rd<uint8_t>(b, hdr::wait_en) != 0;
    inst.waitCnt = rd<uint8_t>(b, hdr::wait_cnt);
    inst.waitVal = rd<uint16_t>(b, hdr::wait_val);
    inst.sigEn = rd<uint8_t>(b, hdr::sig_en) != 0;
    inst.sigCnt = rd<uint8_t>(b, hdr::sig_cnt);
    inst.sigTile = rd<uint16_t>(b, hdr::sig_tile);
    inst.imm = rd<float>(b, hdr::imm);
    inst.groupSize = rd<uint16_t>(b, hdr::group_size);
    inst.auxAddr = rd<uint64_t>(b, hdr::aux_addr);
    for (unsigned k = 0; k < opnd::count; ++k) {
        const uint8_t *p = b + opnd::base + k * opnd::bytes;
        TensorDesc &t = inst.opnd[k];
        t.addr = rd<uint64_t>(p, opnd::addr);
        for (unsigned d = 0; d < opnd::dims; ++d) {
            t.size[d] = rd<uint16_t>(p, opnd::size + 2 * d);
            t.stride[d] = rd<int32_t>(p, opnd::stride + 4 * d);
        }
    }
    return true;
}

void
encode(const TensorInst &inst, uint8_t *b)
{
    using namespace isa;
    std::memset(b, 0, kInstBytes);
    wr<uint8_t>(b, hdr::opcode, static_cast<uint8_t>(inst.opcode));
    wr<uint8_t>(b, hdr::subop, inst.subop);
    wr<uint8_t>(b, hdr::flags, inst.flags);
    wr<uint8_t>(b, hdr::dtype_a, static_cast<uint8_t>(inst.dtype[0]));
    wr<uint8_t>(b, hdr::dtype_b, static_cast<uint8_t>(inst.dtype[1]));
    wr<uint8_t>(b, hdr::dtype_c, static_cast<uint8_t>(inst.dtype[2]));
    wr<uint8_t>(b, hdr::wait_en, inst.waitEn);
    wr<uint8_t>(b, hdr::wait_cnt, inst.waitCnt);
    wr<uint16_t>(b, hdr::wait_val, inst.waitVal);
    wr<uint8_t>(b, hdr::sig_en, inst.sigEn);
    wr<uint8_t>(b, hdr::sig_cnt, inst.sigCnt);
    wr<uint16_t>(b, hdr::sig_tile, inst.sigTile);
    wr<float>(b, hdr::imm, inst.imm);
    wr<uint16_t>(b, hdr::group_size, inst.groupSize);
    wr<uint64_t>(b, hdr::aux_addr, inst.auxAddr);
    for (unsigned k = 0; k < opnd::count; ++k) {
        uint8_t *p = b + opnd::base + k * opnd::bytes;
        const TensorDesc &t = inst.opnd[k];
        wr<uint64_t>(p, opnd::addr, t.addr);
        for (unsigned d = 0; d < opnd::dims; ++d) {
            wr<uint16_t>(p, opnd::size + 2 * d, t.size[d]);
            wr<int32_t>(p, opnd::stride + 4 * d, t.stride[d]);
        }
    }
}

const char *
opcodeName(isa::Opcode op)
{
    switch (op) {
      case isa::Opcode::NOP: return "NOP";
      case isa::Opcode::END: return "END";
      case isa::Opcode::SYNC: return "SYNC";
      case isa::Opcode::DMA_COPY: return "DMA_COPY";
      case isa::Opcode::MATMUL: return "MATMUL";
      case isa::Opcode::VEC_BINARY: return "VEC_BINARY";
      case isa::Opcode::VEC_UNARY: return "VEC_UNARY";
      case isa::Opcode::VEC_REDUCE: return "VEC_REDUCE";
      case isa::Opcode::VEC_SCALAR: return "VEC_SCALAR";
    }
    return "?";
}

std::string
TensorInst::toString() const
{
    std::ostringstream os;
    os << opcodeName(opcode) << " sub=" << unsigned(subop);
    if (waitEn)
        os << " wait c" << unsigned(waitCnt) << ">=" << waitVal;
    if (sigEn)
        os << " sig c" << unsigned(sigCnt)
           << (sigTile == isa::kLocalTile ? "" : "@t" + std::to_string(sigTile));
    for (int k = 0; k < 3; ++k) {
        const TensorDesc &t = opnd[k];
        if (t.addr == 0 && t.numElems() <= 1)
            continue;
        os << " op" << k << "=0x" << std::hex << t.addr << std::dec << "["
           << t.size[3] << "," << t.size[2] << "," << t.size[1] << "," << t.size[0] << "]";
    }
    return os.str();
}

} // namespace gw100
