/**
 * @file  gw100_image.h
 * @brief 固件镜像头与启动信息的格式（启动 ROM 与固件共用）
 *
 * @see spec-mgmt-tile
 */

#ifndef GW100_IMAGE_H
#define GW100_IMAGE_H

#include <stdint.h>

/** 镜像头的 MAGIC：ASCII "GWFW" */
#define GW_IMAGE_MAGIC      0x57465747u
/** 当前的镜像头格式版本 */
#define GW_IMAGE_HDR_VER    1u
/** 镜像头的长度 */
#define GW_IMAGE_HDR_SIZE   64u

/** 固件镜像头，64 B，位于装载区起始处（spec-mgmt-tile §8.2） */
struct gw_image_hdr {
    /** MAGIC，"GWFW" */
    uint32_t magic;
    /** 镜像头格式版本，当前为 1 */
    uint16_t hdr_version;
    /** 镜像头长度，当前为 64 */
    uint16_t hdr_size;
    /** 镜像总长度（含镜像头），等于 host 写入的 FW_SIZE */
    uint32_t image_size;
    /** 固件入口相对装载区起始处的偏移，4 B 对齐 */
    uint32_t entry_offset;
    /** 固件版本号 */
    uint32_t fw_version;
    /** 保留，必须为 0 */
    uint32_t reserved[3];
    /** 偏移 0x40 起的固件内容的 SHA-256 摘要 */
    uint8_t digest[32];
};

/** 启动信息，位于 ROM 保留区起始处（spec-mgmt-tile §8.4） */
struct gw_boot_info {
    /** 启动 ROM 的版本 */
    uint32_t rom_version;
    /** 复位原因：0 冷复位，1 热复位，2 设备完全复位 */
    uint32_t boot_reason;
    /** 本次启动中失败的加载次数 */
    uint32_t load_attempts;
    /** 保留 */
    uint32_t reserved;
    /** SRAM 容量，字节 */
    uint64_t sram_size;
    /** 装载区大小，字节 */
    uint64_t load_max;
};

#endif /* GW100_IMAGE_H */
