# gem5-ext：ACIS 的 gem5 模型

通过 `EXTRAS` 编译进上游 gem5（NULL 构建），不修改 `gem5/`。构建和测试命令见仓库根目录的 `CLAUDE.md`；完整的开发者指南将写成 MAN-060。

## 组件与规格的对应

| 目录 | SimObject / 模块 | 对应规格 | 状态（L1 = 端到端功能正确，未做时序校准） |
|---|---|---|---|
| `func-core/` | 数值规则、指令编解码、Tile 相对地址换算、向量运算（不依赖 gem5） | SPEC-110 / 230 / 240 | L0 通过 |
| `src/compute_tile/sram/` | `AcisBankedSram`：多端口、分 bank 的 Tile SRAM | SPEC-200 | L1 通过 |
| `src/compute_tile/sync/` | `AcisSyncUnit`：同步计数器 | SPEC-250 | L1 通过；跨 Tile 通知未实现 |
| `src/compute_tile/vector/` | `AcisVectorEngine` | SPEC-220 | L1 通过；VEC_BINARY / VEC_SCALAR |
| `src/compute_tile/dma/` | `AcisDmaEngine`：本地口直连 SRAM，远端口接 NoC | SPEC-250 / 320 | L1 通过 |
| `src/compute_tile/controller/` | `AcisTileController`：接收指令、换算本地窗口地址、分发到各单元、处理 NOP/SYNC/END | SPEC-200 / 230 | L1 通过 |
| （待建）`src/compute_tile/matrix/` | 矩阵引擎 | SPEC-210 | 未开始 |
| `src/mgmt_tile/` | `AcisDispatcher`：寄存器、doorbell、命令记录、集中派发 | SPEC-330 / 400 | L1 通过 |
| （待建）`src/noc/` | 轻量 2D Mesh，支持多播；目前用 `NoncoherentXBar` 代替 | SPEC-120 | 未开始 |
| `configs/lpddr5x.py` | LPDDR5X-8533 近似时序 | SPEC-300 | 待按 JEDEC 校准 |
| `src/testhost/` | `AcisTestHost`：代替 Host 的测试平台，不是硬件 | — | 可用 |
| `src/common/` | `MemStreamer`：把字节段拆成访存请求，跟踪完成前缀 | — | 可用 |

## 一次算子测试的流程

1. 仓库根目录下 `tools/gw100_testkit/harness.py` 的 `OpTest` 分配张量、用 `asm.py` 写程序、用 `golden.py` 算期望值，然后在仓库根目录生成测试包 `build/op-tests/<名字>/package.json`。用例本身放在仓库根目录的 `tests/`。
2. `configs/run_op.py` 读测试包，由 `chip.py` 搭建芯片，挂上 `AcisTestHost`。
3. TestHost 后门加载输入、程序和命令记录，然后写 `CMD_ADDR`、敲 `DOORBELL`，并轮询完成记录。
4. 派发器取命令记录和程序并校验，包括每个目标 Tile 上的本地地址是否越界。之后按 tile_mask 下发指令：一条多播指令只占用一次指令通道。各 Tile 控制器收到指令后，先把本地窗口地址换算成本 Tile SRAM 的全局地址，再交给功能单元。所有 Tile 执行完 END 后，派发器写回完成记录。
5. TestHost 后门读回输出，写 `result.json`，结束仿真。harness 读回结果与 golden 比对，并解析 `m5out/stats.txt`。

测量窗口是完成记录里的 `start_tick` 到 `end_tick`：从派发器收到 doorbell 开始，到最后一个 Tile 执行完 END 为止。

## 理想化清单

原则见仓库根目录 `CLAUDE.md`：模型是在模拟硬件，凡是与真实硬件不一致的简化都必须登记在这里。"方向"指这项简化让性能结果偏乐观还是偏悲观。L2 校准前要先消除标为"高"的项，否则校准的是一个不符合物理的模型。

| # | 位置 | 现状 | 方向 | 优先级 | 计划 |
|---|---|---|---|---|---|
| 1 | NoC | 用一个 `NoncoherentXBar` 代替 2D Mesh：没有多跳延迟，也没有沿 XY 路径的链路竞争 | 乐观 | 高 | 自研轻量 Mesh（SPEC-120） |
| 2 | Host 通路 | Host 内存直接挂在片上交叉开关上，延迟 100 ns，没有 PCIe Gen4 x4 的带宽和往返延迟。TestHost 轮询完成记录也要经过片上交叉开关 | 乐观 | 高 | 建 PCIe Tile，Host 内存放到它后面；TestHost 接在 Host 一侧 |
| 3 | DMA 暂存缓冲 | `buf` 按整次传输的大小分配，读方从不因写方慢而被反压 | 写比读慢时乐观 | 高 | 加有限暂存缓冲参数，读方受缓冲剩余空间限制 |
| 4 | 向量引擎 | 先把操作数整块读进内部缓冲，再计算，再整块写回。缓冲大小不受限，读、算、写之间也没有重叠 | 结构不符；大张量隐含无限缓冲，时序上又没有流水重叠 | 高 | 改成有限缓冲的流式流水线，缓冲大小进 `arch_params` |
| 5 | 派发器 | 整个程序先读进一块不限大小的缓冲，再零时间完成译码和校验（END 检查、本地地址越界检查），然后才开始派发 | 校验免费，偏乐观；必须等整个程序取完才开始，偏悲观 | 中 | 有限指令缓冲、流式取指、译码要花周期。越界改为 Tile 换算地址时报错，再由派发器结束命令；整程序检查移到软件（汇编器、编译器、驱动） |
| 6 | 指令多播 | 一条多播指令无论发给多少个 Tile，都只占用一次指令通道 | 依赖物理通道 | 中 | 随"指令通道"决策（docs/README §8.2）定下物理形态，再按该形态计时 |
| 7 | DRAM 时序 | LPDDR5X-8533 由 LPDDR5-6400 派生，tCL 暂沿用 6400 的纳秒值 | 未知 | 中 | 按 JEDEC LPDDR5X 规范逐项核对 |
| 8 | 向量引擎吞吐 | 各种数据类型都按 32 元素/周期计 | 低精度偏悲观 | 低 | 在 SPEC-220 中定义各类型吞吐后再改 |

已消除：
- 2026-10-18：DRAM 容量随测试缩小，器件规格也跟着变。现在只取自 `arch_params`。
- 2026-10-18：派生 LPDDR5X 时把 tCL 也按时钟缩短了，让 DRAM 延迟凭空变小。现在 tCL 保持纳秒值不变。
