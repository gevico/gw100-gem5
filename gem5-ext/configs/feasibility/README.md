# 多 ISA 可行性测试

**可行性测试，不是产品组件。** 验证 gem5 能在同一个进程中同时运行 ARM host、RV64 管理核和多个 RV32 核（Goal 的 M0：1 个管理核加上 Compute Tile 控制核）。产品配置（`run_gw100.py`、`gw100/`）不导入这里的任何文件；RV32 核的系统结构只为验证可行性，不代表 Compute Tile 的设计，不登记理想化，也不进入 `make test`。

| 部分 | 内容 |
|---|---|
| ARM host 与 GW100 设备 | 与 `run_gw100.py` 相同的板卡、host 预设和设备预设；host 运行 `--kernel` 给出的裸机程序，推荐 `boot_ok`（经 BAR2 加载固件并等待 `FW_READY`） |
| RV64 管理核 | 设备预设中的管理系统，运行启动 ROM，再由 host 加载的固件接管 |
| 16 个 RV32 核 | 每个核一个独立的 `RiscvSystem`：RV32IMAC 的 `RiscvTimingSimpleCPU`（M 模式，1 GHz）、自己的交叉开关和 64 KiB `SimpleMemory`（本地地址 0 起），运行以 `CORE_ID` 编译的 `rv32_hello_<k>.elf`（`firmware/feasibility/rv32_hello/`） |

## RV32 程序检查什么

每个核只使用自己的存储，完成以下检查后经 semihosting 输出一行，再以 `SYS_EXIT_EXTENDED` 退出（子码 `0x100 | 失败位 0x80 | 核号`），之后在 `wfi` 中停住：

- **取指与整数运算**：256 轮中，M 扩展的乘法、高位乘法、有符号与无符号的除法和取余，逐项与只用基本指令的移位相加乘法、恢复除法比较；输出的校验和依赖以核号区分的种子，各核互不相同；
- **栈**：递归计算 fib(20) = 6765，在栈上的 256 个字中填写并与闭式结果比较；
- **独立存储**：开始时在 BSS 中写入以核号区分的 1024 个字，其余检查结束后读回比较；已初始化数据须是本核镜像中的值；
- **正常结束**：每个核各自退出；`multi_isa.py` 逐个收集退出事件，直到 ARM 程序和全部 RV32 核都已报告，某个核的退出不结束整个测试。

## 构建与运行

在仓库根目录：

```sh
make gem-build baremetal-build      # gem5、裸机测试（含 boot_ok）、启动 ROM 与固件
make -C firmware/feasibility/rv32_hello BUILD_DIR=$PWD/build/gw100-feasibility
```

在 `gem5/` 目录，两种 host 各运行一次：

```sh
for h in timing atomic; do
    ./build/GW100/gem5.opt -d m5out/feasibility-$h \
        ../gem5-ext/configs/feasibility/multi_isa.py --host $h \
        --kernel ../build/gw100-baremetal/boot_ok.elf
done
```

`--max-ticks` 默认与 `make test` 相同（3 × 10¹² tick，仿真 3 s）；`--rv32-dir`、`--rv32-cores`、`--rom` 见 `--help`。

**成功判定**：进程退出码为 0，输出中有 16 行 `rv32[k] PASS …`，以及逐核的汇总：

```
[feasibility] arm   : PASS at tick …（boot_ok.elf）
[feasibility] rv64  : insts …
[feasibility] rv32_00: PASS at tick … insts …
…
[feasibility] rv32_15: PASS at tick … insts …
[feasibility] 结果：PASS（1 个 ARM、1 个 RV64、16 个 RV32）
```

每个 RV32 核都要有自己的 `PASS` 退出事件和非零的提交指令数（从 `stats.txt` 的 `<系统>.cpu.commitStats0.numInsts` 读出）；RV64 管理核的证据是非零的指令数，以及 ARM 程序 `boot_ok` 的通过（固件已就绪并报告版本）。ARM 程序失败、任何一个核没有报告或报告失败、退出子码重复、到达 tick 上限，结果都是 FAIL。

## 结果（2026-11-04，commit 见提交记录）

| host | 结果 | RV32 核 | RV64 指令数 | 仿真结束 tick | 墙钟 |
|---|---|---|---|---|---|
| timing | PASS | 16/16，各约 59.8 万条指令，在 26.65 – 26.76 ms 之间退出 | 1,317,582 | 203,565,744,819 | 27.2 s |
| atomic | PASS | 16/16，同上 | 1,267,491 | 198,057,032,379 | 23.6 s |

全程共 1118 万条指令，其中 16 个 RV32 核约 956 万条；timing host 下 gem5 进程的峰值内存约 440 MB（`stats.txt` 的 `hostMemory`）。两次运行同时进行，墙钟时间互有影响。

## 发现的问题

- **gem5 的 RISC-V semihosting 只认第一个调用者**：`RiscvSemihosting::portProxyImpl` 用 static 缓存一个绑定第一个调用者的 port proxy，其他核的 semihosting 访存都落到第一个核的内存（输出的字符串、退出参数块都错）。已用 `gem5-ext/patches/0003-arch-riscv-semihosting-port-proxy-per-thread-context.patch` 修复。ARM 的 `ArmSemihosting` 有同样的写法（按安全状态各缓存一个），目前只有一个 ARM 核，不受影响；以后有多个 ARM 系统时要一并修复。

## 源码检查与风险（未在本测试中执行）

| 项 | gem5 的行为 | 对 Compute Tile 的影响 |
|---|---|---|
| hart ID | `mhartid` 读的是本系统内的 contextId（`arch/riscv/isa.cc`），每个独立系统的核都是 0 | 控制核不能用 `mhartid` 区分 Tile；Tile 编号要由 `spec-compute-tile` 定义的本地寄存器提供。若把多个核放进同一个系统，各核又无法在相同的本地地址看到各自的本地存储 |
| WFI | 没有待处理的中断时 `quiesce`，由中断唤醒（`isa/decoder.isa` 的 `wfi`） | 本测试中报告完的核停在 `wfi`，不再产生事件。唤醒需要同一系统内的中断源：NoC 的通知、同步计数器的等待都要建模成控制核的中断输入 |
| `time` CSR | `RiscvSystem::tryReadMtime` 要求系统中有 CLINT，否则 panic | 控制核若读 `time`，每个 Tile 需要自己的计时器或经设计好的方式共享时间基准 |
| 异常 | M 模式的 trap 进入 `mtvec`；本测试设置了异常入口但没有触发 | 非法指令、访存错误等 PROGRAM_FAULT 路径要在 M8 中专门测试 |
| 复位 | `RiscvBareMetal::initState` 在仿真开始时激活线程，没有运行中按住复位、再从复位向量重启的机制 | 管理 Tile 用停机请求加 CRC 的复位时序实现，仍有理想化 #21（停机时在途取指照常执行）；Compute Tile 的复位与隔离要单独设计（决策 Q4），不能照搬 |
| 仿真开销 | 每个 RV32 核是一个独立的 timing CPU | 空闲的核必须停在 `wfi`，否则即使无事可做也每周期取指 |
