/**
 * @file  memory_control.cc
 * @brief MemoryControl 的实现
 *
 * @see spec-dram, spec-address-map
 */

#include "src/gw100/mem_tile/memory_control.hh"

namespace gem5::gw100
{

namespace md = memorydefs;

/** 构造指定编号的 Tile */
MemoryControl::MemoryControl(unsigned tile) : tile(tile)
{}

/** 清除配置和训练结果，递增复位代次 */
void
MemoryControl::reset()
{
    ++epoch;
    released = pllLocked = locked = frozen = false;
    channels = {};
    regions = {};
    errors = 0;
    errorSource = errorAddr = errorTxn = errorInfo = 0;
}

/** 通道处于异步命令期间 */
bool
MemoryControl::busy(unsigned channel) const
{
    const auto s = channels.at(channel).state;
    return s == md::STATE_CALIBRATING || s == md::STATE_TRAINING ||
           s == md::STATE_QUIESCING || s == md::STATE_RESUMING;
}

/** 完成通道命令，保存状态、结果和完成次数 */
void
MemoryControl::complete(unsigned channel, unsigned state, unsigned error)
{
    auto &c = channels.at(channel);
    c.state = state;
    c.error = error;
    ++c.sequence;
    if (error) {
        log(error & 0xFF, md::SOURCE_MGMT, 0, 0,
            channel | (uint64_t(error >> 8) << 8) | (uint64_t(state) << 16));
    }
}

/** 发起一个通道命令，返回错误编码 */
unsigned
MemoryControl::command(unsigned channel, unsigned cmd)
{
    auto &c = channels.at(channel);
    unsigned error = md::ERR_CONFIG;
    if (!released || !pllLocked) {
        error = md::ERR_NOT_READY;
    } else if (busy(channel)) {
        error = md::ERR_BUSY;
    } else {
        switch (cmd) {
            case md::CMD_CONFIGURE:
                if (c.state == md::STATE_RESET &&
                    c.profile == md::PROFILE_ID) {
                    complete(channel, md::STATE_CONFIGURED);
                    return md::ERR_NONE;
                }
                break;
            case md::CMD_CALIBRATE:
                if (c.state == md::STATE_CONFIGURED) {
                    c.state = md::STATE_CALIBRATING;
                } else {
                    break;
                }
                error = md::ERR_NONE;
                break;
            case md::CMD_TRAIN:
                if (c.state == md::STATE_CALIBRATED) {
                    c.state = md::STATE_TRAINING;
                } else {
                    break;
                }
                error = md::ERR_NONE;
                break;
            case md::CMD_ENABLE:
                if (c.state == md::STATE_TEST && c.trained == 3 && locked) {
                    complete(channel, md::STATE_READY);
                    return md::ERR_NONE;
                }
                break;
            case md::CMD_QUIESCE:
                if (c.state == md::STATE_TEST || c.state == md::STATE_READY) {
                    c.runState = c.state;
                    c.state = md::STATE_QUIESCING;
                    error = md::ERR_NONE;
                }
                break;
            case md::CMD_SELF_REFRESH:
                if (c.state == md::STATE_QUIESCED) {
                    c.state = md::STATE_QUIESCING;
                    error = md::ERR_NONE;
                }
                break;
            case md::CMD_RESUME:
                if (c.state == md::STATE_QUIESCED ||
                    c.state == md::STATE_SELF_REFRESH) {
                    c.state = md::STATE_RESUMING;
                    error = md::ERR_NONE;
                }
                break;
        }
        if (error == md::ERR_NONE) {
            c.error = 0;
            if (cmd == md::CMD_CALIBRATE || cmd == md::CMD_TRAIN) {
                c.trained = 0;
            }
            if (onCommand) {
                onCommand(channel, cmd);
            }
            return error;
        }
    }
    c.error = error;
    log(error, md::SOURCE_MGMT, 0, 0, channel);
    return error;
}

/** 校验一个完整数据请求的地址和保护权限 */
bool
MemoryControl::permitted(uint64_t addr, unsigned size, unsigned source) const
{
    if (!size || addr >= md::TOTAL_BYTES || size > md::TOTAL_BYTES - addr) {
        return false;
    }
    if (source == md::SOURCE_MGMT) {
        return true;
    }
    for (const auto &r : regions) {
        if (r.enabled && addr < r.limit && addr + size > r.base) {
            return false;
        }
    }
    return true;
}

/** 记录首个未清除错误及位图 */
void
MemoryControl::log(unsigned error, unsigned source, uint64_t addr,
                   uint64_t txn, uint64_t info)
{
    if (!errors) {
        errorSource = source;
        errorAddr = addr;
        errorTxn = txn;
        errorInfo = info;
    }
    errors |= 1u << error;
}

/** 读取自然对齐的控制寄存器，失败返回 false */
bool
MemoryControl::read(uint64_t off, unsigned size, uint64_t &value) const
{
    value = 0;
    if ((size != 4 && size != 8) || off % size) {
        return false;
    }
    if (off >= md::CHANNEL_BASE) {
        unsigned ch = (off - md::CHANNEL_BASE) / md::CHANNEL_STRIDE;
        if (ch >= channels.size()) {
            return false;
        }
        const auto &c = channels[ch];
        unsigned reg = (off - md::CHANNEL_BASE) % md::CHANNEL_STRIDE;
        if ((reg == md::CH_SEQUENCE || reg == md::CH_CK_PS) != (size == 8)) {
            return false;
        }
        switch (reg) {
            case md::CH_PROFILE:
                value = c.profile;
                break;
            case md::CH_STATE:
                value = c.state;
                break;
            case md::CH_TRAIN_MASK:
                value = c.trained;
                break;
            case md::CH_ERROR:
                value = c.error;
                break;
            case md::CH_SEQUENCE:
                value = c.sequence;
                break;
            case md::CH_CK_PS:
                value = md::CK_PS;
                break;
        }
        return true;
    }
    if (off >= md::PROTECTION_BASE &&
        off < md::PROTECTION_BASE + regions.size() * md::PROTECTION_STRIDE) {
        unsigned i = (off - md::PROTECTION_BASE) / md::PROTECTION_STRIDE;
        unsigned reg = (off - md::PROTECTION_BASE) % md::PROTECTION_STRIDE;
        if ((reg == 0 || reg == 8) != (size == 8)) {
            return false;
        }
        if (reg == 0) {
            value = regions[i].base;
        } else if (reg == 8) {
            value = regions[i].limit;
        } else if (reg == 16) {
            value = regions[i].enabled;
        }
        return true;
    }
    if (off >= md::REG_REQUESTS && off <= md::REG_TRANSFER_TICKS) {
        if (size != 8) {
            return false;
        }
        value = (frozen ? snapshot : counters)[(off - md::REG_REQUESTS) / 8];
        return true;
    }
    bool wide = off == md::REG_EPOCH || off == md::REG_ERROR_ADDR ||
                off == md::REG_ERROR_TXN || off == md::REG_ERROR_INFO;
    if (wide != (size == 8)) {
        return false;
    }
    switch (off) {
        case md::REG_VERSION:
            value = md::VERSION;
            break;
        case md::REG_TOPOLOGY:
            value = tile | (2 << 8) | (2 << 16);
            break;
        case md::REG_STATUS:
            value = (channels[0].state == md::STATE_READY) |
                    ((channels[1].state == md::STATE_READY) << 1) |
                    (!released << 8);
            break;
        case md::REG_CAPABILITIES:
            value = md::CAPABILITIES_VALUE;
            break;
        case md::REG_EPOCH:
            value = epoch;
            break;
        case md::REG_ERROR:
            value = errors;
            break;
        case md::REG_ERROR_SOURCE:
            value = errorSource;
            break;
        case md::REG_ERROR_ADDR:
            value = errorAddr;
            break;
        case md::REG_ERROR_TXN:
            value = errorTxn;
            break;
        case md::REG_ERROR_INFO:
            value = errorInfo;
            break;
        case md::REG_PROTECTION_LOCK:
            value = locked;
            break;
        case md::REG_PERF_FREEZE:
            value = frozen;
            break;
    }
    return true;
}

/** 写入控制寄存器，返回错误编码 */
unsigned
MemoryControl::write(uint64_t off, unsigned size, uint64_t value)
{
    uint64_t ignored;
    if (!read(off, size, ignored)) {
        return md::ERR_PROTOCOL;
    }
    if (off >= md::CHANNEL_BASE) {
        unsigned ch = (off - md::CHANNEL_BASE) / md::CHANNEL_STRIDE;
        unsigned reg = (off - md::CHANNEL_BASE) % md::CHANNEL_STRIDE;
        if (reg == md::CH_COMMAND) {
            return command(ch, value);
        }
        if (reg == md::CH_PROFILE) {
            if (channels[ch].state != md::STATE_RESET) {
                return md::ERR_CONFIG;
            }
            channels[ch].profile = value;
        }
        return md::ERR_NONE;
    }
    if (off >= md::PROTECTION_BASE &&
        off < md::PROTECTION_BASE + regions.size() * md::PROTECTION_STRIDE) {
        if (locked) {
            return md::ERR_PERMISSION;
        }
        unsigned i = (off - md::PROTECTION_BASE) / md::PROTECTION_STRIDE;
        unsigned reg = (off - md::PROTECTION_BASE) % md::PROTECTION_STRIDE;
        if (reg == 0) {
            regions[i].base = value;
        } else if (reg == 8) {
            regions[i].limit = value;
        } else if (reg == 16) {
            regions[i].enabled = value & 1;
        }
        return md::ERR_NONE;
    }
    if (off == md::REG_PROTECTION_LOCK && (value & 1)) {
        for (const auto &r : regions) {
            if (r.enabled && (r.base >= r.limit || r.limit > md::TOTAL_BYTES ||
                              (r.base | r.limit) % 4096)) {
                return md::ERR_CONFIG;
            }
        }
        locked = true;
    } else if (off == md::REG_ERROR) {
        errors &= ~value;
    } else if (off == md::REG_PERF_FREEZE) {
        if ((value & 1) && !frozen) {
            snapshot = counters;
        }
        frozen = value & 1;
    }
    return md::ERR_NONE;
}

} // namespace gem5::gw100
