/**
 * @file  test.c
 * @brief 测试结果的报告与退出
 */

#include <stdarg.h>

#include "bm/console.h"
#include "bm/semihost.h"
#include "bm/test.h"

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

/* start.S 在 test_main 返回后调用 */
__attribute__((noreturn)) void bm_test_exit(int rc);

void
bm_test_exit(int rc)
{
    if (rc == 0) {
        bm_printf("[PASS]\n");
        bm_semihost_exit(BM_EXIT_PASS);
    }
    bm_printf("[FAIL] test_main 返回 %d\n", rc);
    bm_semihost_exit(BM_EXIT_FAIL);
}
