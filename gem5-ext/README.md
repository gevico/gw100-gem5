# gem5-ext：GW100 的 gem5 模型

通过 `EXTRAS` 编译进上游 gem5（NULL 构建），不修改 `gem5/`。完整的开发者指南将写成 MAN-060。

## 构建

命令都从仓库根目录开始执行。第一次构建是全量的，要十分钟以上；之后只改 gem5-ext 时，增量构建一两分钟。改了 C++ 或 SimObject 的 Python 参数之后要重新构建，构建不会自动发生。

macOS（变体目录 `build/NULL_DARWIN`）：

```sh
uv pip install --python .venv/bin/python -r requirements-dev.txt   # 含 scons
ROOT=$PWD; cd gem5
../.venv/bin/scons defconfig build/NULL_DARWIN build_opts/NULL      # 只需一次
../.venv/bin/scons build/NULL_DARWIN/gem5.opt EXTRAS="$ROOT/gem5-ext" \
    PYTHON_CONFIG=/opt/homebrew/opt/python@3.12/bin/python3.12-config -j8
```

用 Homebrew 的 Python 3.12 嵌入 gem5，本机默认的 3.14 太新。

Linux（以 Ubuntu 24.04 为例，变体目录 `build/NULL`）：先装好 `build-essential m4 scons python3-dev`，这是 gem5 上游列出的最小依赖（`gem5/util/dockerfiles/ubuntu-24.04_min-dependencies/Dockerfile`），再执行：

```sh
ROOT=$PWD; cd gem5
scons build/NULL/gem5.opt EXTRAS="$ROOT/gem5-ext" -j$(nproc)
```

## 建模约定

建模的最高原则见 [CONTRIBUTING.md](../CONTRIBUTING.md) §5：模型是在模拟硬件，不能为了软件或测试的方便脱离实际。在此之下：

- 代码全部放在 `gem5-ext/`，通过 `EXTRAS` 编进 gem5，不改 gem5 本体。确实绕不开的本体改动，做成补丁放进 `gem5-ext/patches/`，并注明原因。
- 按张量指令粒度建模，不逐个 MAC 仿真。
- ISA 不放进 `gem5/src/arch`：张量指令由 Tile 的 SimObject 自己译码；编码以生成的 `isa_defs.hh` 为准，单一真源待重建。
- 片上网络计划用自研的轻量 2D Mesh，粒度是 DMA 传输，支持多播；Garnet 只用来交叉验证。目前暂时用交叉开关代替，见理想化清单第 1 项。
- DRAM 为 LPDDR5X-8533，从 gem5 自带的 LPDDR5-6400 派生，共 8 个 x16 通道。

## 组件与规格的对应

> 下表的 L1 状态是删除测试（2026-10-24）之前的记录；当前没有自动化测试复现这些结论。

| 目录 | SimObject / 模块 | 对应规格 | 状态（L1 = 端到端功能正确，未做时序校准） |
|---|---|---|---|
| `src/gw100/pcie/` | `Gw100PciDevice`：PciEndpoint、BAR0、VERSION/STATUS/CMD_ADDR/DOORBELL | SPEC-310 / 400 | 最小枚举验证 |
| （待建）`src/noc/` | 轻量 2D Mesh，支持多播；目前用 `NoncoherentXBar` 代替 | SPEC-120 | 未开始 |

当前只恢复了 PCIe endpoint 骨架；原来的 func-core、Compute Tile、管理 Tile 和 common 已删除，后续按新的 PCIe 模型逐步重建。

## 测试

原有的设备侧端到端测试（`tests/l1_inst/`）和 L0 单元测试已按 2026-10-24 的决定全部删除，仓库里不再有 `tests/` 目录，也没有可运行的 pytest 入口。

新的测试方案以全栈 E2E 为目标：应用 → runtime → 驱动 → Linux → PCIe → 设备。旧的 TestHost、testkit 和 device-side 配置已经删除；方案落地前不要恢复它们。

## 开发注意

- PCIe endpoint 继承 `PciEndpoint`，Host 使用 `VExpress_GEM5_Foundation` 的 `GenericArmPciHost`；BAR0 为 4 KiB。
- 旧的 TestHost 和 device-side 配置已删除；GW100 现在走 ARM board 的 VExpress PCIe。

## 理想化清单

原则见仓库根目录 `CLAUDE.md`：模型是在模拟硬件，凡是与真实硬件不一致的简化都必须登记在这里。"方向"指这项简化让性能结果偏乐观还是偏悲观。L2 校准前要先消除标为"高"的项，否则校准的是一个不符合物理的模型。

> 旧模型删除后，以下清单待按新的 PCIe 模型重建。

| # | 位置 | 现状 | 方向 | 优先级 | 计划 |
|---|---|---|---|---|---|
| 1 | NoC | 用一个 `NoncoherentXBar` 代替 2D Mesh：没有多跳延迟，也没有沿 XY 路径的链路竞争 | 乐观 | 高 | 自研轻量 Mesh（SPEC-120） |
| 2 | PCIe 通路 | VExpress 的 PCIe 是功能模型，没有 Gen4 x4 的链路时序和带宽 | 未知 | 中 | 加 PCIe 链路 shim；新 ARM board 已接到 VExpress PCIe |
| 3 | DMA 暂存缓冲 | `buf` 按整次传输的大小分配，读方从不因写方慢而被反压 | 写比读慢时乐观 | 高 | 加有限暂存缓冲参数，读方受缓冲剩余空间限制 |
| 4 | 向量引擎 | 先把操作数整块读进内部缓冲，再计算，再整块写回。缓冲大小不受限，读、算、写之间也没有重叠 | 结构不符；大张量隐含无限缓冲，时序上又没有流水重叠 | 高 | 改成有限缓冲的流式流水线，缓冲大小进架构参数（待重建） |
| 5 | 派发器 | 整个程序先读进一块不限大小的缓冲，再零时间完成译码和校验（END 检查、本地地址越界检查），然后才开始派发 | 校验免费，偏乐观；必须等整个程序取完才开始，偏悲观 | 中 | 有限指令缓冲、流式取指、译码要花周期。越界改为 Tile 换算地址时报错，再由派发器结束命令；整程序检查移到软件（汇编器、编译器、驱动） |
| 6 | 指令多播 | 一条多播指令无论发给多少个 Tile，都只占用一次指令通道 | 依赖物理通道 | 中 | 随"指令通道"决策（docs/README §5.2）定下物理形态，再按该形态计时 |
| 7 | DRAM 时序 | LPDDR5X-8533 由 LPDDR5-6400 派生，tCL 暂沿用 6400 的纳秒值 | 未知 | 中 | 按 JEDEC LPDDR5X 规范逐项核对 |
| 8 | 向量引擎吞吐 | 各种数据类型都按 32 元素/周期计 | 低精度偏悲观 | 低 | 在 SPEC-220 中定义各类型吞吐后再改 |

已消除：
- 2026-10-18：DRAM 容量随测试缩小，器件规格也跟着变。现在只取自架构参数。
- 2026-10-18：派生 LPDDR5X 时把 tCL 也按时钟缩短了，让 DRAM 延迟凭空变小。现在 tCL 保持纳秒值不变。
