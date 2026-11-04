/**
 * @file  int_test.c
 * @brief 中断请求的测试固件：CTX_VECTOR、INT_LEVEL、INT_REQ
 *
 * 只编进 fw_int.img（GW_FW_INT_TEST），供裸机测试 int_fw 使用。host 在
 * 加载固件之前配置好 MSI-X 并使能。固件初始化之后：把上下文 3 映射到
 * 向量 5，置位 INT_LEVEL.ADMIN，再请求 ADMIN、FW_EVENT 和 CTX[3] 的中断，
 * 然后置位 FW_READY（spec-pcie §6.5、§8.4）。
 *
 * @see spec-pcie
 */

#include <stdint.h>

#include "gw100_mgmt.h"

/** 发出测试用的中断请求；返回 0 表示寄存器读回正确 */
int
int_selftest(void)
{
    if (gw_read32(GW_CTX_VECTOR(3)) != 5u)   /* 复位值 2 + n */
        return 1;
    gw_write32(GW_CTX_VECTOR(3), 5u);
    gw_write32(GW_INT_LEVEL, GW_INT_ADMIN);
    if (gw_read32(GW_INT_LEVEL) != GW_INT_ADMIN)
        return 2;
    /* 先写映射与 INT_LEVEL，再写 INT_REQ：同方向 posted 写按顺序生效 */
    gw_write32(GW_INT_REQ, GW_INT_ADMIN | GW_INT_FW_EVENT | GW_INT_CTX(3));
    return 0;
}
