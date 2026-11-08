/**
 * @file  sha256.h
 * @brief 不依赖标准库的 SHA-256，供启动 ROM 校验镜像
 *
 * @see spec-mgmt-tile
 */

#ifndef GW100_SHA256_H
#define GW100_SHA256_H

#include <stddef.h>
#include <stdint.h>

/** 计算 data 起 len 字节的 SHA-256，结果写入 out（32 字节） */
void sha256(const uint8_t *data, size_t len, uint8_t out[32]);

#endif /* GW100_SHA256_H */
