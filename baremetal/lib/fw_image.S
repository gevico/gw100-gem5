/**
 * @file  fw_image.S
 * @brief 把管理 Tile 的固件镜像（fw.img、fw_wdt.img）嵌入裸机程序
 *
 * 路径由 Makefile 经 GW100_FW_IMAGE、GW100_FW_WDT_IMAGE 传入；镜像由
 * make firmware-build 生成（tools/fwpack.py）。fw_wdt.img 是看门狗的
 * 测试固件，只有 wdt_fatal 使用。
 */
    .section .rodata
    .balign 8
    .globl gw100_fw_image
gw100_fw_image:
    .incbin GW100_FW_IMAGE
    .globl gw100_fw_image_end
gw100_fw_image_end:

    .balign 8
    .globl gw100_fw_wdt_image
gw100_fw_wdt_image:
    .incbin GW100_FW_WDT_IMAGE
    .globl gw100_fw_wdt_image_end
gw100_fw_wdt_image_end:
