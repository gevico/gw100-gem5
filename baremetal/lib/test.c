/**
 * @file  test.c
 * @brief 测试结果的报告与退出
 */

#include <stdarg.h>

#include "bm/console.h"
#include "bm/semihost.h"
#include "bm/test.h"

/** 打印"文件:行"和 printf 格式的失败信息，以 BM_EXIT_FAIL 结束仿真；不返回 */
void
bm_test_fail(const char *file, int line, const char *fmt, ...)
{
    va_list ap;

    bm_printf("[FAIL] %s:%d: ", file, line);
    va_start(ap, fmt);
    bm_vprintf(fmt, ap);
    va_end(ap);
    bm_printf("\n");
    bm_semihost_exit(BM_EXIT_FAIL);
}

/**
 * start.S 在 test_main 返回后调用：rc 为 0 时打印 [PASS] 并以 BM_EXIT_PASS
 * 退出，否则打印 [FAIL] 并以 BM_EXIT_FAIL 退出；不返回
 */
__attribute__((noreturn)) void
bm_test_exit(int rc)
{
    if (rc == 0) {
        bm_printf("[PASS]\n");
        bm_semihost_exit(BM_EXIT_PASS);
    }
    bm_printf("[FAIL] test_main 返回 %d\n", rc);
    bm_semihost_exit(BM_EXIT_FAIL);
}
