# firmware：管理 Tile 的 RISC-V 程序

管理 Tile 的管理 CPU（RV64IMAC，M/U 模式）运行这里的程序，它们与 host 的 ARM 裸机程序在同一个 gem5 进程中运行（`gem5-ext/configs/gw100/devices/mgmt.py`）。

| 目录 | 内容 | 规格 |
|---|---|---|
| `rom/` | 启动 ROM：等 SRAM 清零和 ML 建立，写 STAGE = 1、2，等待 `FW_START` 或 FLR，校验镜像头和 SHA-256，设置 PMP、清零运行区、写启动信息后跳转 | `spec-mgmt-tile` §8、§9 |
| `fw/` | 固件。入口发布 STAGE = 4、FW_READY = 0，等待 `PLL_TS` 锁定（以 `mcycle` 计超时），把 `clk_mgmt`、`clk_core` 切换到 `PLL_CORE`（等锁定超时 1 ms），使能并锁定看门狗（10 ms），经 dram.c 配置、训练和自检设备内存，再设置并锁定 HDMA 描述符区间的访问控制、初始化 HDMA 通道 0、1 的环，写入固件版本，在 DRAM、HDMA、HDI 全部成功后置位 `FW_READY`；主循环在 `wfi` 中等中断（`fw/irq.c`：PLIC 的 doorbell 信箱、事件、ML 错误、看门狗第一级，CLINT 定时器每 1/4 看门狗周期唤醒一次，另可设置唤醒期限），每次醒来喂狗、处理 HDI 与 FLR（确认后重新初始化 HDMA）。`fw/hdi.c` 是 HDI 的命令处理（CP）：HDI_CC 的 BUSY 握手、控制器的初始化、复位与关闭，doorbell 的检查与 `ERR_REQ`，SQE 与 CQE 经通道 0 和设备内存顶端的暂存区传输（CQE 分两次写出），管理命令（NOP 至 ABORT）与上下文命令（NOP、COPY、FILL、SIGNAL_EVENT、WAIT_EVENT、TIMESTAMP；LOAD_PROGRAM、UNLOAD_PROGRAM、LAUNCH 待 Compute Tile 建模后实现），COPY 经通道 1 按 1 MiB 分段，上下文的错误状态、错误日志和中断合并；每个队列同时只执行一条命令；初始化失败时写 ERROR = 4、STAGE = 0xF 后停住。以 `GW_FW_WDT_TEST`、`GW_FW_HDMA_TEST`、`GW_FW_INT_TEST` 编译分别得到看门狗、HDMA 和中断请求的测试固件 `fw_wdt.img`、`fw_hdma.img`、`fw_int.img`，它们不处理 HDI | `spec-mgmt-tile` §8.5、§9，`spec-hdi` |
| `common/` | 寄存器地址（`gw100_mgmt.h`）、镜像头与启动信息（`gw100_image.h`）、FLR 的取走与确认、HDMA 的最小驱动（`gw100_hdma.h`）、SHA-256 | |
| `feasibility/` | 可行性测试的程序，不是产品固件，`make firmware-build` 不构建它们。`rv32_hello/` 是多 ISA 可行性测试中 RV32 核运行的程序（每个核一份以 `CORE_ID` 编译的实例），构建与运行见 `gem5-ext/configs/feasibility/README.md` | |

## 构建

需要 `riscv64-elf-gcc`（macOS：`brew install riscv64-elf-gcc`）。在仓库根目录：

```sh
make firmware-build     # 同时生成正常和兼容固件，各自使用独立输出目录
```

- `rom.elf` 是启动 ROM 的镜像，`run_gw100.py` 默认用它（`--rom` 可改）；gem5 的 `RiscvBareMetal` 在仿真开始时把它装入 ROM。
- `fw.img` 是固件镜像：`fw.elf` 的二进制加上 `tools/fwpack.py` 填写的 64 B 镜像头（`spec-mgmt-tile` §8.2）。裸机测试经 `baremetal/lib/fw_image.S` 把它嵌入 host 程序，由 `gw100_boot()` 按 `spec-pcie` §6.4 的驱动流程经 BAR2 写入设备。

`fw_wdt.img` 是看门狗的测试固件：初始化中自检第一级超时，置位 `FW_READY` 后不再喂狗，只写心跳，等第二级超时停住 CPU。`baremetal/lib/fw_image.S` 同样把它嵌入，只有 `wdt_fatal` 测试加载它。`fw_hdma.img` 是 HDMA 的测试固件（`fw/hdma_test.c`）：不经命令协议，按与 host 约定的地址（`baremetal` 的 `BM_DMA_BUF_BASE`）做传输，结果写入 `FW_VERSION`，只有 `hdma_basic` 加载它。`fw_int.img`（`fw/int_test.c`）设置 `CTX_VECTOR`、`INT_LEVEL` 后发出 `INT_REQ`，只有 `int_fw` 加载它。

当前的 `fw.img` 是测试用固件：以 `GW_FW_DEBUG_WIN` 编译，初始化和每次 FLR 之后开启 BAR2 调试窗口，供 `pcie_bar2` 等测试使用。正式固件只在 host 经 SET_FEATURE 请求时开启（`spec-pcie` §7.3）；固件已实现 SET_FEATURE(DEBUG_WINDOW)，正式镜像不定义 `GW_FW_DEBUG_WIN` 即可。

启动 ROM 在 25 MHz 下校验镜像的 SHA-256，校验时间随镜像大小线性增长：约 10 KB 的 `fw.img` 从写入到 `FW_READY` 约 170 ms 仿真时间。

## DRAM 初始化和生命周期

fw/dram.c 共用于正常、看门狗、HDMA 和中断测试固件。它通过管理 NI 的
正常 timing 路径执行控制器配置、校准、训练、双 rank 首末行自检、排空
和保护表锁定。自检地址与错误位置保存在 gw_dram_failure 中。
校准、训练的反馈时延和阶段超时由共享内存档案生成。
--dram-corrupt-mask 可按全局 rank 位注入自检读取错误，验证固件定位。

进入固件即发布 STAGE = 4、FW_READY = 0。等待期间持续喂狗并服务 FLR。
初始化失败发布 STAGE = 0xF，FW_VERSION 中的 0xD2 诊断包含 Tile、通道、
rank、阶段和原因。FLR 保留 DRAM 配置与数据，恢复由统一启动状态控制。
D3hot 排空并进入自刷新，D0 恢复后重建 HDMA/HDI 并发布就绪。
完整复位重新从 ROM 加载和初始化。详细协议见 spec-dram。

正常镜像位于 build/gw100-firmware，MEMORY_PROFILE=compat 的镜像位于
build/gw100-firmware-compat。根目录构建规则也将两组镜像嵌入各自的
裸机 ELF 输出目录，测试矩阵据此选择。

## 约定

- 不链接标准库；编译器隐式需要的 `memcpy`、`memset` 在 `common/string.c`。
- 启动 ROM 没有已初始化的可写数据，BSS 和栈在 SRAM 末尾的 ROM 保留区，等 `SRAM_STATUS.INIT_DONE` 之后才使用。
- 经 ML 访问 HIF 寄存器区只能用 32 位访问（`spec-pcie` §6.5）；链路故障时访问产生异常，启动 ROM 与固件停住，等待 host 复位设备。
- 每个函数、宏和结构体字段都要有 `/** */` 注释（`CONTRIBUTING.md` §7）。
