/**
 * @file  fw_image.S
 * @brief 把管理 Tile 的固件镜像（fw.img）嵌入裸机程序
 *
 * 路径由 Makefile 经 GW100_FW_IMAGE 传入；镜像由 make firmware-build
 * 生成（tools/fwpack.py）。
 */
    .section .rodata
    .balign 8
    .globl gw100_fw_image
gw100_fw_image:
    .incbin GW100_FW_IMAGE
    .globl gw100_fw_image_end
gw100_fw_image_end:
