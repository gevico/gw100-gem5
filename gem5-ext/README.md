# gem5-ext：GW100 的 gem5 模型

通过 `EXTRAS` 编译进上游 gem5 的 ARM 构建（host 为 AArch64），不修改 `gem5/`。完整的开发者指南将写成 `man-gem5-dev`。

## 构建

命令都从仓库根目录开始执行。第一次构建是全量的，要十分钟以上；之后只改 gem5-ext 时，增量构建一两分钟。改了 C++ 或 SimObject 的 Python 参数之后要重新构建：`make gem-build`、`make run`、`make test` 会比较 `gem5-ext/src/` 下的源文件和 gem5 子模块的提交号，有变化才调用 scons，没有变化时直接跳过。改了 `PYTHON_CONFIG` 等构建选项时，用 `make -B gem-build` 强制构建。只改 `gem5-ext/configs/` 下的配置脚本不需要重新构建。

```sh
uv pip install --python .venv/bin/python -r requirements-dev.txt   # 含 scons
make check        # 检查工具链、Python，以及 gem5.opt 链接的 Python 版本
make gem-build    # 构建 gem5/build/GW100/gem5.opt 和 compile_commands.json
```

`build/GW100` 同时编译 ARM 和 RISC-V 两个 ISA：ARM 用于 host，RISC-V 用于管理 Tile 和 Compute Tile 的控制核，两者运行在同一个仿真进程中。第一次构建时，`make gem-build` 以 `build_opts/ARM` 为基础生成构建配置，再打开 `USE_RISCV_ISA`。

gem5 嵌入的 Python 要与 `.venv` 同一版本（3.12），`make gem-build` 会自动找对应的 `python3.12-config`。手工构建时注意：gem5 只从**环境变量**读 `PYTHON_CONFIG`，写成 scons 参数不生效，会退回系统默认的 `python3-config`（macOS 上是 Homebrew 的 3.14）：

```sh
cd gem5
../.venv/bin/scons defconfig build/GW100 build_opts/ARM EXTRAS="$PWD/../gem5-ext"
../.venv/bin/scons setconfig build/GW100 USE_RISCV_ISA=y
PYTHON_CONFIG=/opt/homebrew/bin/python3.12-config ../.venv/bin/scons \
    --ignore-style build/GW100/gem5.opt EXTRAS="$PWD/../gem5-ext" -j8
```

Linux（以 Ubuntu 24.04 为例）：先装好 `build-essential m4 scons python3-dev`，这是 gem5 上游列出的最小依赖（`gem5/util/dockerfiles/ubuntu-24.04_min-dependencies/Dockerfile`），裸机程序另需 `gcc-aarch64-linux-gnu`，然后同样执行 `make gem-build`。

## 建模约定

建模的最高原则见 [CONTRIBUTING.md](../CONTRIBUTING.md) §5：模型是在模拟硬件，不能为了软件或测试的方便脱离实际。在此之下：

- 代码全部放在 `gem5-ext/`，通过 `EXTRAS` 编进 gem5，不改 gem5 本体。确实绕不开的本体改动，做成补丁放进 `gem5-ext/patches/`，并注明原因。
- **目录树即组件树**：一个目录是组件树上的一个节点，放这个节点自己的类；只有一个类的叶子组件作为文件放在父目录中，自身还有子组件的建子目录；被多个子树共用的组件放在它们的最近公共祖先下。组件之间的接口放在共同父节点的 `*_types.hh` 中。每个目录一个 `SConscript`，只声明本目录的源文件；单元测试不放在 `src/` 中（见"测试"）。目录关系因此就是组成关系；跨子树的引用（例如 `bar0/window_regs` 使用 `window/window_table`）只经头文件，并在类注释中写明。
- 按张量指令粒度建模，不逐个 MAC 仿真。
- ISA 不放进 `gem5/src/arch`：张量指令由 Tile 的 SimObject 自己译码；编码将由重建后的单一真源生成（旧的 `isa_defs.hh` 已随 func-core 删除）。
- 片上网络计划用自研的轻量 2D Mesh，粒度是 DMA 传输，支持多播；Garnet 只用来交叉验证。Compute Tile 重建之前还没有片上互连。
- DRAM 为 LPDDR5X-8533，从 gem5 自带的 LPDDR5-6400 派生，共 8 个 x16 通道。

## 组件与规格的对应

| 目录 | SimObject / 模块 | 对应规格 | 状态 |
|---|---|---|---|
| `src/gw100/pcie/` | `Gw100PciDevice`：外壳，只负责 PciEndpoint 的入口、`mem_port`、ML 的 HIF 侧接入、功能级复位的编排和 checkpoint；`Gw100PcieFactory` 组装下列组件，接口见 `pcie_types.hh`。`status/`：`StatusUnit`（ERR/INT）；`boot/`：`BootCtrl`（STAGE、启动窗口、提交计数、FW_START 门控、启动通路故障、FLR 门控与确认）；`hif/`：`HifRegs`（固件可见的 HIF 寄存器区，ML 的 HIF 侧目标）；`window/`：`WindowTable`（BAR2 调试窗口）；`config/`：`ConfigSpace`，其下 `capability/`：`Capability` 基类、`PmCapability`、`PcieCapability`；`bar0/`：`Bar0Handler` 与 `GlobalRegs`、`WindowRegs`、`BootRegs`；`bar2/`：`Bar2Handler`（启动窗口、调试窗口、关闭）、`DevmemBackend`、`Gw100PrefetchMemBar` | `spec-pcie` | 寄存器层与启动流程完成：裸机测试 `boot_ok`、`boot_errors`、`boot_flr` 覆盖 F10、F11 的启动部分，`pcie_*` 覆盖 F1–F7、F11/F12 的寄存器部分；`ConfigSpace`、`WindowTable`、`StatusUnit` 有单元测试。中断投递、MSI-X、DMA、HDI 未实现 |
| `src/gw100/ml/` | `Gw100MgmtLink`：ML 的两端适配器 `MlEndpoint` 与四个 `AsyncFifo`，两端分别由 PCIe 设备（`clk_pcie`）和管理 Tile（`clk_mgmt`）驱动 | `spec-mgmt-link` | 完成；`MlEndpoint` 有单元测试（建立、顺序、tag、屏障、撤销、超时与故障、延迟与吞吐） |
| `src/gw100/mgmt/` | `Gw100MgmtTile`：外壳，本地总线端口、ML 管理侧目标、PLIC 中断源。`sram/`：`MgmtSram`（ROM、按 bank 排队的 SRAM、复位后的硬件清零）；`mailbox/`：`MgmtMailbox`（doorbell 信箱、事件寄存器）；`regs/`：`MgmtLocalRegs`（看门狗、ML、SRAM 控制、访问控制）；`crc/`：`MgmtCrc`（复位原因、PLL 锁定、无毛刺时钟切换；切换 `clk_mgmt` 时改变管理系统时钟域的频率档）。管理 CPU（`RiscvTimingSimpleCPU`，RV64，M/U）、CLINT、PLIC 用 gem5 的设备，在 `configs/gw100/devices/mgmt.py` 中组成独立的 RiscvSystem | `spec-mgmt-tile` | 启动相关部分完成，运行真实的 RISC-V 启动 ROM 与固件（`firmware/`）；时钟切换完成；看门狗计时、NoC 端口未实现 |
| （待建）`src/noc/` | 轻量 2D Mesh，支持多播 | `spec-noc` | 未开始 |

原来的 func-core、Compute Tile 和 common 已删除，按新的规格逐步重建。

## 测试

设备级测试是在 host 上运行的裸机程序，放在 `baremetal/tests/`，每个 test 独立运行，共用 `baremetal/lib/` 的公共库。在仓库根目录执行 `make test`，默认在 `timing` 和 `atomic` 两个 host 预设下各跑一遍；架构、约定和用法见 `baremetal/README.md`。

组件的单元测试用 gem5 自带的 GTest，放在 `tests/unit/` 下，目录结构与 `src/` 一一对应，例如 `src/gw100/pcie/config/config_space.cc` 的测试是 `tests/unit/gw100/pcie/config/config_space.test.cc`。只为不依赖 gem5 `Packet` 的纯逻辑组件写单元测试，例如 `ConfigSpace`、`WindowTable`、`StatusUnit`。

测试按标签引用被测的源文件，不写相对路径：组件的 `SConscript` 用 `Source(..., add_tags='gw100 pcie config')` 给源文件打标签，测试目录的 `SConscript` 用 `GTest(..., with_tag('gw100 pcie config'), with_tag('gem5 serialize'))` 取用。组件移动位置时测试不受影响。

构建并运行一个测试（在 `gem5/` 目录，`PYTHON_CONFIG` 与 `make gem-build` 相同）：

```sh
scons --ignore-style EXTRAS=<仓库根目录>/gem5-ext \
    build/GW100/gem5-ext/tests/unit/gw100/pcie/config/config_space.test.opt
./build/GW100/gem5-ext/tests/unit/gw100/pcie/config/config_space.test.opt
```

全栈 E2E 测试（应用 → runtime → 驱动 → Linux → PCIe → 设备）的方案尚未落地。原有的 `tests/l1_inst/`、L0 单元测试、TestHost 和 testkit 已于 2026-10-24 删除，不要恢复。

## 开发注意

- PCIe endpoint 继承 `PciEndpoint`，Host 使用 `VExpress_GEM5_Foundation` 的 `GenericArmPciHost`；BAR0 为 1 MiB，BAR2 为 64 MiB（spec-pcie §5）。
- `VExpress_GEM5_Foundation` 没有接 GICv3 ITS，目前只能用 INTx；做 MSI/MSI-X 时要换成 `VExpress_GEM5_V2` 或加上 ITS。
- 旧的 TestHost 和 device-side 配置已删除；GW100 现在走 ARM board 的 VExpress PCIe。

## 理想化清单

原则见仓库根目录 `CLAUDE.md`：模型是在模拟硬件，凡是与真实硬件不一致的简化都必须登记在这里。"方向"指这项简化让性能结果偏乐观还是偏悲观。L2 校准前要先消除标为"高"的项，否则校准的是一个不符合物理的模型。

编号沿用，不复用；`Idealization:` 脚注引用的是这里的编号。

| # | 位置 | 现状 | 方向 | 优先级 | 计划 |
|---|---|---|---|---|---|
| 2 | PCIe 通路 | VExpress 的 PCIe 是功能模型，没有 Gen4 x4 的链路时序和带宽；BAR 访问固定花 `pioDelay` | 未知 | 中 | 加 PCIe 链路 shim |
| 9 | BAR2 窗口 | 经 `mem_port` 以功能访问读写设备内存，固定花 `bar2_latency`（200 ns），不与设备内部的其他访问竞争 | 乐观 | 中 | 性能建模阶段改为 timing 访问 |
| 10 | 设备内存 | 用 `SimpleMemory` 代替 LPDDR5X（32 GiB），没有 DRAM 的时序、带宽和通道 | 乐观 | 高（DMA 接入后） | 接入 LPDDR5X 控制器模型 |
| 12 | FLR、D3hot→D0 | 复位立即完成，不占时间 | 乐观 | 低 | 加复位时间参数 |
| 13 | PCIe capability | 只实现 PM 和 PCI Express；AER、DSN、LTR、MSI-X 未实现，Device Status 的错误位和 Transactions Pending 从不置位；中断只更新 `INT_STATUS`，不投递；固件可见的 `INT_REQ`、`INT_LEVEL`、`CTX_VECTOR` 未实现 | 功能缺失 | 中 | MSI-X 与 INTx 在中断阶段实现，AER 随错误处理实现 |
| 15 | CLINT | 用 gem5 的 Clint：`mtime` 可被写入（规格要求只读）；`PLL_TS` 的锁定延迟不建模，时间戳从仿真开始即计数 | 乐观 | 低 | 换成自己的 CLINT，按规格只读 |
| 16 | 启动窗口的入站写入 | 合法写入拆成 `BOOT_WR` 后进入 ML 客户端队列，队列不设上限，不反压 host；当前 host 一次只有一个未完成的 MMIO 写，不会积压 | 乐观 | 中 | 待批准；或建模有限的入站缓冲和对 host 的反压 |
| 17 | 看门狗 | 只实现寄存器，不计时，不产生第一级中断和第二级的停机与致命错误 | 功能缺失 | 中 | 随固件框架实现 |
| 18 | 设备完全复位 | `BOOT_CTRL.DEV_RESET` 的写入被忽略 | 功能缺失 | 中 | 实现设备完全复位，复位管理系统与 ML |
| 19 | SRAM ECC | 校验位不建模，没有故障注入，不会产生可纠正或不可纠正错误；相关寄存器存在 | — | 低 | 需要时加故障注入 |
| 20 | CRC 的 Mesh 与 Compute Tile 控制 | `CRC_MESH_RST`、`CRC_CT_RST`、`CRC_CT_CLK_EN`、`CRC_CT_ISO` 按规格读写，但还没有 NoC、Memory Tile 和 Compute Tile 的模型可以作用；`CRC_ISO_ACK`、`CRC_ISO_FAIL` 读 0；`clk_core` 的切换只改寄存器，`PLL_DDR` 锁定后不驱动任何时钟 | 功能缺失 | 中 | 随 NoC、Memory Tile、Compute Tile 的模型接入 |

随旧模型删除而失效（2026-10-24），重建对应组件时不得重犯：

| # | 原位置 | 当时的理想化 | 重建时的要求 |
|---|---|---|---|
| 1 | NoC | 用 `NoncoherentXBar` 代替 2D Mesh，没有多跳延迟和链路竞争 | 自研轻量 Mesh（`spec-noc`） |
| 3 | DMA 暂存缓冲 | 按整次传输大小分配，读方从不被反压 | 有限暂存缓冲，读方受剩余空间限制 |
| 4 | 向量引擎 | 操作数整块读入不限大小的缓冲，读、算、写不重叠 | 有限缓冲的流式流水线，缓冲大小进架构参数 |
| 5 | 派发器 | 整个程序读进不限大小的缓冲，零时间译码和校验 | 有限指令缓冲、流式取指，译码花周期；整程序检查移到软件 |
| 6 | 指令多播 | 多播指令无论发给多少 Tile 都只占一次指令通道 | 定下指令通道的物理形态后，按该形态计时 |
| 7 | DRAM 时序 | LPDDR5X-8533 由 LPDDR5-6400 派生，tCL 沿用 6400 的纳秒值 | 按 JEDEC LPDDR5X 规范逐项核对 |
| 8 | 向量引擎吞吐 | 各数据类型都按 32 元素/周期计 | 按 `spec-vector-engine` 定义的各类型吞吐 |

已消除：
- 2026-11-03：启动寄存器上电即为 STAGE 4 且 `FW_READY`、调试窗口始终开启（原 #11）。现在由真实的 RISC-V 启动 ROM 推进 STAGE，host 经启动窗口加载固件，固件置位 `FW_READY` 并开启调试窗口。
- 2026-10-18：DRAM 容量随测试缩小，器件规格也跟着变。现在只取自架构参数。
- 2026-10-18：派生 LPDDR5X 时把 tCL 也按时钟缩短了，让 DRAM 延迟凭空变小。现在 tCL 保持纳秒值不变。
