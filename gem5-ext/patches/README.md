# gem5 本体的补丁

代码全部放在 `gem5-ext/`，不改 `gem5/` 子模块（`CONTRIBUTING.md`）。确实绕不开的本体问题，做成补丁放在这里，每个补丁的开头写明原因、修改和验证。

## 怎样应用

- `make gem-build` 在调用 scons 之前运行 `make gem5-patch`：按文件名顺序处理本目录的 `*.patch`。能反向应用的说明已经打过，跳过；能正向应用的就应用（打印 `[PATCH] 已应用 …`）；两者都不行时报错停止（例如升级 gem5 后补丁失效）。
- 补丁文件是 gem5 构建的输入之一：新增或修改补丁会触发重新构建。
- 应用后 `gem5/` 的工作区处于修改状态，主仓库的 `git status` 会显示 `gem5 (modified content)`，这是预期的。升级 gem5 时先在子模块里撤销补丁（`git -C gem5 checkout -- .`），更新指针，再重新构建；补丁无法应用时更新或删除它。
- 只用 `git apply`，macOS 和 Linux 上相同。

## 补丁清单

| 补丁 | 问题 | 平台 |
|---|---|---|
| `0001-base-fiber-keep-ucontext-out-of-the-class-layout.patch` | macOS 上 `ucontext_t` 的布局取决于第一次包含 `<ucontext.h>` 前是否定义了 `_XOPEN_SOURCE`，`Fiber` 内嵌它导致不同编译单元的布局不一致（ODR 违例）：gem5 的 `fiber.test` 失败、`coroutine.test` 段错误，GICv3 ITS 处理第一条命令即崩溃。改为 `Fiber` 持有指针，`ucontext_t` 只在 `fiber.cc` 中使用 | 修复 macOS；Linux 上行为不变 |
| `0002-dev-arm-gicv3-eoir1-group-with-ds.patch` | `GICD_CTLR.DS` = 1 时全部 Group 1 中断为 G1NS，但在 EL3 写 `ICC_EOIR1_EL1` 时 gem5 降的是 G1S 的优先级，运行优先级降不回来，之后同优先级的中断都发不出去。改为 DS = 1 时按 G1NS 处理 | 与平台无关；DS = 0 时行为不变 |
| `0003-arch-riscv-semihosting-port-proxy-per-thread-context.patch` | `RiscvSemihosting` 用一个 static 的 port proxy 访问内存，它绑定第一个发起 semihosting 调用的 ThreadContext；其他 hart（包括其他 `RiscvSystem` 的核）的 semihosting 访存都落到第一个 hart 所在系统的内存。改为每个 ThreadContext 一个 proxy | 与平台无关；只有一个 hart 调用 semihosting 时行为不变 |

前三项修复上游问题。以下补丁提供本项目的可选接口和恢复支持。

| 补丁 | 用途 | 验证 |
|---|---|---|
| `0004-dev-asynchronous-pio-hooks.patch` | PioDevice 可选异步 timing 完成与 retry，复用原生地址范围通知端口 | BAR2、FLR 和 HDI COPY 回归 |
| `0005-mem-controller-lifecycle.patch` | 固件训练后显式启动媒体，局部排空实际写入，自刷新进入、恢复与控制器复位 | 组件排空、重复电源循环、复位和恢复测试 |
| `0006-mem-sparse-checkpoint-restore.patch` | 以固定缓冲解压并比较，只写入有差异的块，保留按需分配。检查截断文件，沿用原生 gzip checkpoint 格式 | 完整 32 GiB 就绪和自刷新 checkpoint 恢复 |
