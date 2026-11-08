/**
 * @file  serializer.cc
 * @brief PCIe 精确串行时钟的实现
 *
 * @see spec-pcie
 */

#include "src/gw100/pcie/link/serializer.hh"

#include <algorithm>
#include <stdexcept>

#include "src/gw100/pcie/link/generated/link_defs.hh"

namespace gem5::gw100::pcie
{

/** 构造指定速率和宽度的单方向串行器。 */
Serializer::Serializer(unsigned generation, unsigned width, uint64_t origin_ps)
{
    reset(generation, width, origin_ps);
}

/** 物理链路重新建立或换速率时重建串行相位。 */
void
Serializer::reset(unsigned generation, unsigned width, uint64_t origin_ps)
{
    if (generation < 1 || generation > 4 ||
        (width != 1 && width != 2 && width != 4)) {
        throw std::invalid_argument("PCIe generation or width");
    }
    s = State{};
    s.generation = generation;
    s.width = width;
    constexpr uint64_t bit_half[] = {
        pciedefs::GEN1_BIT_HALF_PS,
        pciedefs::GEN2_BIT_HALF_PS,
        pciedefs::GEN3_BIT_HALF_PS,
        pciedefs::GEN4_BIT_HALF_PS,
    };
    s.bitHalf = bit_half[generation - 1];
    s.endHalf = 2 * origin_ps;
    s.dataOriginHalf = s.endHalf;
    s.nextSkpHalf = s.endHalf + skpIntervalHalf();
}

/** 标准 SKP 的物理长度，单位 bit/lane。 */
unsigned
Serializer::skpBits() const
{
    return s.generation < 3 ? 40 : 130;
}

/** SKP 调度周期，单位 0.5 ps。 */
uint64_t
Serializer::skpIntervalHalf() const
{
    if (s.generation < 3) {
        return pciedefs::SKP_SYMBOLS_INTERVAL * 10 * s.bitHalf;
    }
    return pciedefs::SKP_BLOCKS_INTERVAL * 130 * s.bitHalf;
}

/** 开始新数据流，计入 SDS 和 Gen4 初始 Control SKP。 */
FrameTime
Serializer::beginStream(uint64_t earliest_ps)
{
    FrameTime frame;
    frame.beginHalf = std::max(2 * earliest_ps, s.endHalf);
    frame.endHalf = frame.beginHalf;
    if (s.generation >= 3) {
        frame.orderedHalf = 130 * s.bitHalf;
        if (s.generation == 4) {
            frame.orderedHalf *= 2;
        }
        frame.endHalf += frame.orderedHalf;
    }
    s.endHalf = frame.endHalf;
    s.dataOriginHalf = s.endHalf;
    s.dataSymbols = 0;
    if (!s.active || s.generation == 4) {
        s.nextSkpHalf = s.endHalf + skpIntervalHalf();
    }
    s.dataStream = true;
    s.active = true;
    s.controlSkpNext = false;
    return frame;
}

/** 某个数据符号开始的绝对相位，单位 0.5 ps。 */
uint64_t
Serializer::symbolStart(uint64_t symbol) const
{
    if (s.generation < 3) {
        return s.dataOriginHalf + symbol * 10 * s.bitHalf;
    }
    return s.dataOriginHalf +
           ((symbol / 16) * 130 + 2 + (symbol % 16) * 8) * s.bitHalf;
}

/** 前 count 个数据符号结束的绝对相位，单位 0.5 ps。 */
uint64_t
Serializer::symbolsEnd(uint64_t count) const
{
    if (s.generation < 3) {
        return s.dataOriginHalf + count * 10 * s.bitHalf;
    }
    uint64_t bits = (count / 16) * 130;
    if (count % 16) {
        bits += 2 + (count % 16) * 8;
    }
    return s.dataOriginHalf + bits * s.bitHalf;
}

/** 填入 IDL，找到不早于指定相位的下一完整符号。 */
void
Serializer::idleUntil(uint64_t half_ps)
{
    if (half_ps <= symbolStart(s.dataSymbols)) {
        return;
    }
    uint64_t delta = half_ps - s.dataOriginHalf;
    uint64_t symbols = 0;
    if (s.generation < 3) {
        uint64_t period = 10 * s.bitHalf;
        symbols = (delta + period - 1) / period;
    } else {
        uint64_t block = delta / (130 * s.bitHalf);
        uint64_t remainder = delta % (130 * s.bitHalf);
        symbols = block * 16;
        if (remainder > 2 * s.bitHalf) {
            uint64_t period = 8 * s.bitHalf;
            symbols += (remainder - 2 * s.bitHalf + period - 1) / period;
        }
    }
    s.dataSymbols = std::max(s.dataSymbols, symbols);
}

/** 发送含完整 framing 的包，长度须为完整 DWORD，单位 B。 */
FrameTime
Serializer::packet(unsigned bytes, uint64_t earliest_ps)
{
    if (!s.dataStream || !bytes || (bytes & 3)) {
        throw std::invalid_argument("PCIe packet outside data stream");
    }
    idleUntil(2 * earliest_ps);
    FrameTime frame;
    uint64_t before = s.dataSymbols;
    frame.beginHalf = symbolStart(before);
    s.dataSymbols += bytes / s.width;
    frame.endHalf = symbolsEnd(s.dataSymbols);
    if (s.generation >= 3) {
        uint64_t headers = (s.dataSymbols + 15) / 16 - (before + 15) / 16;
        frame.encodingHalf = headers * 2 * s.bitHalf;
    }
    s.endHalf = frame.endHalf;
    return frame;
}

/** 在当前包尾之后发送一个周期 SKP，包含 EDS 和必要块尾填充。 */
FrameTime
Serializer::skip(uint64_t earliest_ps)
{
    if (!s.dataStream) {
        throw std::invalid_argument("PCIe SKP outside data stream");
    }
    idleUntil(2 * earliest_ps);
    FrameTime frame;
    frame.beginHalf = symbolStart(s.dataSymbols);
    frame.skpCount = 1;
    if (s.generation < 3) {
        frame.orderedHalf = skpBits() * s.bitHalf;
        frame.endHalf = frame.beginHalf + frame.orderedHalf;
        s.dataOriginHalf += frame.orderedHalf;
    } else {
        uint64_t next_block = (s.dataSymbols / 16 + 1) * 16;
        if (next_block - s.dataSymbols < 4 / s.width) {
            next_block += 16;
        }
        uint64_t block_end = symbolsEnd(next_block);
        frame.paddingHalf = block_end - frame.beginHalf;
        frame.orderedHalf = 130 * s.bitHalf;
        frame.endHalf = block_end + frame.orderedHalf;
        s.dataSymbols = next_block;
        s.dataOriginHalf += frame.orderedHalf;
        if (s.generation == 4) {
            s.controlSkpNext = !s.controlSkpNext;
        }
    }
    s.endHalf = frame.endHalf;
    s.nextSkpHalf += skpIntervalHalf();
    return frame;
}

/** 在数据流以外发送 TS，满足数量和持续时间并计入 SKP/EIEOS。 */
FrameTime
Serializer::training(unsigned sets, uint64_t earliest_ps,
                     uint64_t minimum_duration_ps)
{
    if (s.dataStream || (!sets && !minimum_duration_ps)) {
        throw std::invalid_argument("PCIe training in data stream");
    }
    FrameTime frame;
    frame.beginHalf = std::max(s.endHalf, 2 * earliest_ps);
    if (!s.active) {
        s.nextSkpHalf = frame.beginHalf + skpIntervalHalf();
    }
    s.active = true;
    uint64_t position = frame.beginHalf;
    uint64_t set_bits = s.generation < 3 ? 160 : 130;
    uint64_t minimum_end = frame.beginHalf + 2 * minimum_duration_ps;
    for (unsigned count = 0; count < sets || position < minimum_end; ++count) {
        while (position >= s.nextSkpHalf) {
            position += skpBits() * s.bitHalf;
            s.nextSkpHalf += skpIntervalHalf();
            ++frame.skpCount;
        }
        position += set_bits * s.bitHalf;
        ++s.trainingCount;
        if (s.generation > 1 && s.trainingCount == 32) {
            position += set_bits * s.bitHalf;
            s.trainingCount = 0;
        }
    }
    frame.endHalf = position;
    frame.orderedHalf = position - frame.beginHalf;
    s.endHalf = position;
    return frame;
}

/** 结束数据流并发送 Recovery.RcvrLock 所需的 EIEOS。 */
FrameTime
Serializer::beginRecovery(uint64_t earliest_ps)
{
    FrameTime frame;
    frame.beginHalf = std::max(s.endHalf, 2 * earliest_ps);
    if (!s.active) {
        s.nextSkpHalf = frame.beginHalf + skpIntervalHalf();
    }
    if (s.dataStream) {
        idleUntil(2 * earliest_ps);
        frame.beginHalf = symbolStart(s.dataSymbols);
        if (s.generation >= 3) {
            uint64_t next_block = (s.dataSymbols / 16 + 1) * 16;
            if (next_block - s.dataSymbols < 4 / s.width) {
                next_block += 16;
            }
            frame.paddingHalf = symbolsEnd(next_block) - frame.beginHalf;
        }
    }
    if (s.generation > 1) {
        frame.orderedHalf = (s.generation == 2 ? 160 : 130) * s.bitHalf;
    }
    frame.endHalf = frame.beginHalf + frame.paddingHalf + frame.orderedHalf;
    s.endHalf = frame.endHalf;
    s.dataStream = false;
    s.active = true;
    s.trainingCount = 0;
    return frame;
}

/** 发送 EIOS 并结束当前数据流，保留电气空闲的串行起点。 */
FrameTime
Serializer::electricalIdle(uint64_t earliest_ps)
{
    if (!s.active) {
        throw std::invalid_argument("PCIe EIOS during electrical idle");
    }
    FrameTime frame;
    frame.beginHalf = std::max(s.endHalf, 2 * earliest_ps);
    if (s.dataStream) {
        idleUntil(2 * earliest_ps);
        frame.beginHalf = symbolStart(s.dataSymbols);
    }
    if (s.generation < 3) {
        unsigned symbols = s.generation == 1 ? 4 : 8;
        frame.orderedHalf = symbols * 10 * s.bitHalf;
        frame.endHalf = frame.beginHalf + frame.orderedHalf;
    } else {
        uint64_t block_end = frame.beginHalf;
        if (s.dataStream) {
            uint64_t next_block = (s.dataSymbols / 16 + 1) * 16;
            if (next_block - s.dataSymbols < 4 / s.width) {
                next_block += 16;
            }
            block_end = symbolsEnd(next_block);
            frame.paddingHalf = block_end - frame.beginHalf;
        }
        frame.orderedHalf = 130 * s.bitHalf;
        frame.endHalf = block_end + frame.orderedHalf;
    }
    s.dataStream = false;
    s.active = false;
    s.endHalf = frame.endHalf;
    return frame;
}

/** 恢复已验证、已排空的相位状态。 */
void
Serializer::restore(const State &state)
{
    reset(state.generation, state.width, 0);
    if (state.bitHalf != s.bitHalf || state.endHalf < state.dataOriginHalf) {
        throw std::invalid_argument("PCIe serializer checkpoint state");
    }
    s = state;
}

} // namespace gem5::gw100::pcie
