# gem5-ext：GW100 的 gem5 模型

通过 `EXTRAS` 编译进上游 gem5 的 ARM 构建（host 为 AArch64），不修改 `gem5/`。完整的开发者指南将写成 `man-gem5-dev`。

## 构建

命令都从仓库根目录开始执行。第一次构建是全量的，要十分钟以上；之后只改 gem5-ext 时，增量构建一两分钟。改了 C++ 或 SimObject 的 Python 参数之后要重新构建，构建不会自动发生。

```sh
uv pip install --python .venv/bin/python -r requirements-dev.txt   # 含 scons
make check        # 检查工具链、Python，以及 gem5.opt 链接的 Python 版本
make gem-build    # 构建 gem5/build/ARM/gem5.opt 和 compile_commands.json
```

gem5 嵌入的 Python 要与 `.venv` 同一版本（3.12），`make gem-build` 会自动找对应的 `python3.12-config`。手工构建时注意：gem5 只从**环境变量**读 `PYTHON_CONFIG`，写成 scons 参数不生效，会退回系统默认的 `python3-config`（macOS 上是 Homebrew 的 3.14）：

```sh
cd gem5
PYTHON_CONFIG=/opt/homebrew/bin/python3.12-config ../.venv/bin/scons \
    --ignore-style build/ARM/gem5.opt EXTRAS="$PWD/../gem5-ext" -j8
```

Linux（以 Ubuntu 24.04 为例）：先装好 `build-essential m4 scons python3-dev`，这是 gem5 上游列出的最小依赖（`gem5/util/dockerfiles/ubuntu-24.04_min-dependencies/Dockerfile`），裸机程序另需 `gcc-aarch64-linux-gnu`，然后同样执行 `make gem-build`。

## 建模约定

建模的最高原则见 [CONTRIBUTING.md](../CONTRIBUTING.md) §5：模型是在模拟硬件，不能为了软件或测试的方便脱离实际。在此之下：

- 代码全部放在 `gem5-ext/`，通过 `EXTRAS` 编进 gem5，不改 gem5 本体。确实绕不开的本体改动，做成补丁放进 `gem5-ext/patches/`，并注明原因。
- 按张量指令粒度建模，不逐个 MAC 仿真。
- ISA 不放进 `gem5/src/arch`：张量指令由 Tile 的 SimObject 自己译码；编码将由重建后的单一真源生成（旧的 `isa_defs.hh` 已随 func-core 删除）。
- 片上网络计划用自研的轻量 2D Mesh，粒度是 DMA 传输，支持多播；Garnet 只用来交叉验证。Compute Tile 重建之前还没有片上互连。
- DRAM 为 LPDDR5X-8533，从 gem5 自带的 LPDDR5-6400 派生，共 8 个 x16 通道。

## 组件与规格的对应

| 目录 | SimObject / 模块 | 对应规格 | 状态 |
|---|---|---|---|
| `src/gw100/pcie/` | `Gw100PciDevice`：PciEndpoint、BAR0、VERSION/STATUS/CMD_ADDR/DOORBELL | `spec-pcie`、`spec-hdi` | 已挂到 PCIe 总线（输出目录的 `config.ini` 可见）；还没有软件枚举或访问它，没有自动化测试 |
| （待建）`src/noc/` | 轻量 2D Mesh，支持多播 | `spec-noc` | 未开始 |

当前只恢复了 PCIe endpoint 骨架；原来的 func-core、Compute Tile、管理 Tile 和 common 已删除，后续按新的 PCIe 模型逐步重建。

## 测试

原有的设备侧端到端测试（`tests/l1_inst/`）和 L0 单元测试已按 2026-10-24 的决定全部删除，仓库里不再有 `tests/` 目录，也没有可运行的 pytest 入口。

新的测试方案以全栈 E2E 为目标：应用 → runtime → 驱动 → Linux → PCIe → 设备。旧的 TestHost、testkit 和 device-side 配置已经删除；方案落地前不要恢复它们。

## 开发注意

- PCIe endpoint 继承 `PciEndpoint`，Host 使用 `VExpress_GEM5_Foundation` 的 `GenericArmPciHost`；BAR0 为 4 KiB。
- `VExpress_GEM5_Foundation` 没有接 GICv3 ITS，目前只能用 INTx；做 MSI/MSI-X 时要换成 `VExpress_GEM5_V2` 或加上 ITS。
- 旧的 TestHost 和 device-side 配置已删除；GW100 现在走 ARM board 的 VExpress PCIe。

## 理想化清单

原则见仓库根目录 `CLAUDE.md`：模型是在模拟硬件，凡是与真实硬件不一致的简化都必须登记在这里。"方向"指这项简化让性能结果偏乐观还是偏悲观。L2 校准前要先消除标为"高"的项，否则校准的是一个不符合物理的模型。

编号沿用，不复用；`Idealization:` 脚注引用的是这里的编号。

| # | 位置 | 现状 | 方向 | 优先级 | 计划 |
|---|---|---|---|---|---|
| 2 | PCIe 通路 | VExpress 的 PCIe 是功能模型，没有 Gen4 x4 的链路时序和带宽；BAR 访问固定花 `pioDelay` | 未知 | 中 | 加 PCIe 链路 shim |

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
- 2026-10-18：DRAM 容量随测试缩小，器件规格也跟着变。现在只取自架构参数。
- 2026-10-18：派生 LPDDR5X 时把 tCL 也按时钟缩短了，让 DRAM 延迟凭空变小。现在 tCL 保持纳秒值不变。
