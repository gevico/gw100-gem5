# PCIe G0–G3 验收记录

## 基线与范围

本轮在 `feat/gw100-pcie-endpoint` 工作分支实施。协议基线为登记在
BASE4-CB 的 PCI Express 4.0 Version 1.0-c 原版公开镜像。固定副本的
SHA-256 为 `7d5c27b38f119459158c2b337052d4298ec53e12e89f11280b139726b830abdf`。
来源版本、渲染页核验和后续勘误边界见数字链路契约及共享覆盖矩阵。

默认 Gen4 x4，支持 Gen1–Gen4 和 x1/x2/x4 的十二种组合，单 PF、VC0。
RC/EP 均为 250 MHz，每侧两周期适配，单向传播 5 ns，全局 1 ps。
共享档案拥有缓冲、队列、等待和故障参数，生成模型与裸机定义。

当前交付覆盖通用数字协议、参考 RC/EP 微架构，以及正常裸机
timing/O3 软件路径。模拟电气反馈由数字 PHY 夹具提供。性能验收比较
独立程序与逐边沿计算，具体硬件的校准列入后续关口。

## 实施落点

| 部分 | 文件或目录 |
|---|---|
| 共享参数与来源 | `spec-src/pcie/gw100-link.json`、`tools/pcie_spec.py` |
| 包与 CRC | `gem5-ext/src/gw100/pcie/link/packet.*` |
| 串行与 SKP | 同目录 `serializer.*` |
| 信用与 replay | 同目录 `flow_control.*`、`replay.*` |
| DLL 与数字训练 | 同目录 `link_model.*`、`link_training.cc` |
| 事务账本与完成 | 同目录 `link_transport.*` |
| 原生系统接入 | `gem5-ext/src/gw100/pcie/gw100_pci_link.cc` |
| 子系统恢复 | `gw100_pci_checkpoint.cc`、`link/link_checkpoint.cc` |
| 独立参考 | `tools/pcie_verify.py`、`gem5-ext/tests/pcie/link_fixture.cc` |
| 正常软件与恢复矩阵 | `tools/pcie_system_verify.py`、`baremetal/test-matrix.json` |

ECAM、BAR0、BAR2、完整 HDMA TLP、MSI-X、INTx 和错误消息共享链路。
原生 Host PCI 总线拥有 BDF、动态 BAR 范围和地址路由。RC 在整包接收后
才访问 Host timing 内存后端，后端按缓存行拆包。设备端连接真实固件和
四 Memory Tile、八通道、32 GiB 通用 LPDDR5-8433。

## 验证方法与记录

Python 参考使用多项式除法计算 CRC，以绝对编码块位置计算串行边沿，
从原始字节解析头字段并核对信用、排序、资源占用和数据。参考程序
保持独立的调度和合法性计算。有限资源预算作为共同规格输入。

每个性能场景预热 20 µs，测量 100 µs，数据种子为 1。结果包含命令、
实际配置、源码与可执行文件摘要、逐事件 JSONL、计数和偏差。
确定性孤立写比较源适配、首符号、末符号、传播及目标可见时刻，
允许的最大量化误差为 1 ps。190 个合法配置实际最大时刻误差为 0 ps，
吞吐最大绝对偏差为 4.702%，门槛为 ±5%。

| 验证 | 结果 | 原始记录 |
|---|---|---|
| 共享规格与资源约束 | 25/25 通过 | `tools/pcie_spec_test.py` |
| 独立参考固定向量 | 11/11 通过 | `tools/pcie_verify_test.py` |
| 扩展组件 | 17 个程序，193/193 通过 | `/tmp/gw100-pcie-accepted-gtest.log` |
| 原生布尔数组序列化 | 34/34 通过，保留一个既有 disabled 用例 | `/tmp/gw100-pcie-native-serialize-fixed.log` |
| 最终独立性能矩阵 | 208/208 通过 | `build/pcie-link/accepted/report.json` |
| 最终裸机矩阵 | 67/67 通过，8 项能力跳过 | `gem5/m5out/pcie-accepted/` |
| 最终系统矩阵 | 59/59 通过 | `build/pcie-system-accepted/results.json` |
| L0/L1 三进程恢复 | 3/3 通过 | `build/pcie-system-accepted/results.json` |
| 既有内存组件与恢复 | 21/21 通过 | `build/pcie-memory-validation-fixed/results.json` |

输出目录由工具创建并保留在本地构建区。日志中的字段摘要固定相应轮次
的源码和镜像，后续覆盖元数据收尾与原运行摘要分别记录。

### 验收轮次指纹

完整矩阵对应的档案和可执行文件摘要如下。全部逐文件摘要保存在各轮
JSON 记录中。

```text
validation profile SHA-256
9b909987bb852a40d0b1491018b52d41f42b211d64debca343d9ae2091782adb
native gem5 SHA-256
bf5377a6b6e2638c2abcb4f7442d13f6af965972f3246ef5c47a1ff1b660cd3c
link_transport.cc SHA-256
e37a2be5115feb7aef671a076b5ed4ad01c22f8a774212cce9ed652de4ce63cb
```

关口收尾更新覆盖定位和验收状态。`build/pcie-link/closure.json` 记录
前后摘要，并校验三个生成定义的代码内容保持一致，仅来源摘要发生变化。
完整参数与协议契约摘要在该记录中另存。收尾构建与冒烟单独保留指纹。
收尾构建再次通过组件 193/193、timing/O3 链路冒烟 6/6、跨进程恢复 3/3、
DMA 双向矩阵 6/6，记录分别保存在 `gem5/m5out/pcie-closure/`、
`build/pcie-checkpoint-closure/`、`build/pcie-link/closure/`。

## 资源选择与回归修复

RC/EP 后端各有 64 个完整操作槽，接收、待发、replay 和完成预留分别计量。
RC 的 NP 最多占 48 个槽，为 posted 保留 16 个，仍允许 EP 保有 64 个
未完成读，其余请求通过 RX 信用和队列反压。该分类策略属于参考 RC 设计。
独立参考分别使用 EP 64、RC 48 的 NP 目标上界，纯双向小读的结果受此约束。

这一选择由 EP 同时发起 MRd/MWr 的场景验证。原共享池在 MPS 128 B 下
出现读占槽导致写停顿，写方向低于独立线速方程约 5.3%。固定总容量并
加入分类准入后，六个 DMA 双向场景通过前轮检查。最终值列入性能结果。
原失败记录保留于 `build/pcie-link/dma-checked/report.json`。

普通读等待首次到达时已有的 posted 写可见。依赖固定后，后续写仍可越过
阻塞读。`ReadWaitsForPostedVisibility` 先复现旧值读出，修复后与
`ReadDoesNotWaitForYoungerPosted` 共同验证数据可见性及进展。
每端依赖存储最多对应 16×64 位矩阵及索引元数据。

启动窗口写持有 EP 目标槽直至 ML 写入确认。完成表最多 64 项，每个
最大 TLP 最多形成五个 ML 子写。设备复位结算旧回调，避免旧代次再次更新
提交计数。`pcie_link` 的线上检查核对 ROM 装载期的 ML 往返下界。
旧实现的过早 `TARGET_END` 在原 trace 中由该检查复现。

原生恢复还修复了 libc++ `vector<bool>` 的代理类型输出，以及恢复前
构造或初始化阶段安排的 tick 0 事件。事件启动移至 `startup()`，在
`loadState()` 完成后安排。恢复测试实际使用三个新 gem5 进程。

## 时延与吞吐结果

矩阵含十二模式的读写、两档 MPS、单向与双向基准，Gen4 x4 另外扫描
MRRS 128/256/512 B、RCB 64/128 B、完成上限 64/128/256 B 和三档目标服务。
EP 同时发起 MRd/MWr 的六个场景单列。完成上限小于 RCB 的十八项预期
拒绝，合法参数的结果逐项进行事件、数据、资源、时延和吞吐检查。

以下为 Gen4 x4 的真实 DMA 发包角色。EP 发 MRd 得到 H2D 完成数据，
并同时向 RC 发 D2H MWr。MRRS 为 512 B，RCB 为 64 B，读账本容量为
64，写服务为 250 ns。完成上限为 256 B，受有效 MPS 约束。

| MPS | 读目标服务 | H2D GB/s | D2H GB/s | 相对参考的最大绝对偏差 |
|---|---|---|---|---|
| 128 B | 0 ns | 6.277 | 6.487 | 0.79% |
| 128 B | 250 ns | 6.272 | 6.487 | 0.79% |
| 128 B | 2.5 µs | 6.272 | 6.487 | 0.79% |
| 256 B | 0 ns | 6.876 | 6.925 | 1.21% |
| 256 B | 250 ns | 6.876 | 6.927 | 1.18% |
| 256 B | 2.5 µs | 6.876 | 6.925 | 1.21% |

Gen4 x4、MPS 256 B 的双向参考上界约为 H2D 6.945、D2H 7.010 GB/s。
参考通过两个方向的包、ACK、FC 线量联立求解，编码与 SKP 持续占用
计入基准。参考固定向量另外验证双向线量守恒。

### 十二模式基准

下表由 RC 发 MRd，EP 目标服务为 2.5 µs，MPS/完成上限为 256 B，
MRRS 为 512 B，RCB 为 64 B。孤立写为 64 B，时延含同样的目标服务。
所有孤立写时刻另与独立逐边沿期望比较。

| 链路 | 单向读有效载荷 GB/s | 孤立写 ACCEPT→VISIBLE ns |
|---|---|---|
| Gen1 x1 | 0.225 | 2863.000 |
| Gen1 x2 | 0.445 | 2695.000 |
| Gen1 x4 | 0.901 | 2611.000 |
| Gen2 x1 | 0.445 | 2692.000 |
| Gen2 x2 | 0.901 | 2608.000 |
| Gen2 x4 | 1.797 | 2564.000 |
| Gen3 x1 | 0.881 | 2610.000 |
| Gen3 x2 | 1.766 | 2566.000 |
| Gen3 x4 | 3.533 | 2544.000 |
| Gen4 x1 | 1.766 | 2571.750 |
| Gen4 x2 | 3.538 | 2547.750 |
| Gen4 x4 | 7.071 | 2539.750 |

### 系统耗时分项

正常 timing `hdi_copy` 本轮输出位于
`gem5/m5out/pcie-accepted/gw100/timing/hdi_copy/`。
统计覆盖该用例的固件加载、管理命令、COPY 与错误检查全程。

| 层次 | 观测值 | 计量依据 |
|---|---|---|
| PCIe RC/EP 包串行占用 | 277.131 / 307.533 µs | `pcie-report.json` 的 `packet_ps` |
| PCIe RC/EP 维护占用 | 各约 1505.90 µs | 同文件 `maintenance_ps`，包含空闲时的维护 |
| PCIe RC/EP 排队累计 | 766.110 / 336.759 µs | 同文件 `queue_ps` |
| RC Host 目标累计服务 | 2536.39 µs | 同文件 `target_ps[0]`，包含 Host timing 后端 |
| Host DRAM 全程平均读访问 | 23.570 ns，含排队 6.078 ns | 两个原生 Host 控制器按读 burst 加权 |
| 设备 DRAM 平均读访问 | 114.287 ns，含排队 85.315 ns | 八通道按读 burst 加权 |
| HDMA 临时内存入口 | CDC 15.714、排队 80.807、传输拍 9.974 ns/请求 | `memory.json` 的 HDMA 入口累计占用除以请求数 |
| 整套软件用例跨度 | 292.883 ms | `stats.txt` 的 `simTicks` |

DRAM 数值来自 `totMemAccLat`、`totQLat`、`readBursts`。入口计数以
各自硬件边界解释，累计占用包含并行操作，表中数值可相互重叠。
单个软件访问的 Host RTT 与设备服务在 `pcie_link` 中分别观测。

| Host | BAR0 Host RTT | BAR2 Host RTT | BAR0 设备服务范围 | BAR2 设备服务范围 |
|---|---|---|---|---|
| timing | 477 ns | 557 ns | 48.250–64.625 ns | 56.375–56.500 ns |
| O3 | 279 ns | 636 ns | 48.250–64.625 ns | 56.375–56.500 ns |

Host RTT 使用 25165824 Hz 架构计数器，约 39.74 ns 一档，包含 CPU 与
Host 原生总线。设备服务由精确 trace 按 RX_COMPLETE 到首个完成符号
计算，两个 Host 的 BAR0 样本分别为 21185、21179，BAR2 各三项，均满足
500 ns、1 µs 的条件要求。启动写各有 1530 项，ML 确认后的服务为
104–140 ns，超过该 ROM 装载期 88 ns 的 CDC 往返下界。

正常与 CRC 故障的 timing/O3 trace 均通过独立检查。正常 trace 分别有
561568、556568 个事件，CRC 场景分别有 1033、947 个事件。

## 生命周期与错误覆盖

组件覆盖 ACK/NAK、有限重放存储、丢 ACK、序号与信用回绕、重复包、
CRC、framing、完成拆包、Byte Count、Lower Address、BCM、poison、
UR/CA/CRS、完成缺失与超时、真实 tag、隔离、取消和背压。
原生故障矩阵包含七类线上故障、两档附加 Host 服务、PHY 能力限制和
四个训练失败检查点。失败按预期的有界诊断和退出判定。

FLR 保留规定的链路字段与 sticky AER，主动请求在提交前取消、提交后隔离。
D3hot/D0 覆盖链路 L1/Recovery、媒体排空、自刷新与恢复。PERST、Hot Reset、
Link Down 重新训练及枚举。Transactions Pending 和 Link Status 由实际状态驱动。

隔离期限之后，旧完成与新请求的 Requester ID、tag、TC、地址和长度
完全相同时，线上字段可以匹配新请求。`WireIdenticalTagReuseBoundary`
固定这一可观察边界。隔离期限须与实际系统的最大迟到期限一同选取。

## 后续边界

- 数字 PHY 的默认检测、均衡反馈和 ASPM 空闲阈值为参考选择。链路编码、
  串行占用、协议步骤和规定等待由模型执行。
- 商业 Root Port 配置空间与系统 RAS 后续接入。当前 RC 错误由 trace 和
  计数记录，EP 实现 AER。MSE 关闭时，未解码访问可在原生 Host 总线上拒绝。
- PCIe 文件恢复夹具使用正常 Host 端口和原生 EP，设备后端为 4 KiB。
  八通道 32 GiB 内存保存由独立内存组件验证。管理 Tile 的完整状态保存与
  跨子系统一致性仍须单独验收，整机 checkpoint 关口继续保留。
- ECRC、10 位 tag、多 VC、SRIS、L1 子状态、DSN、LTR 按当前广告能力关闭。
  冻结基线之后的勘误适用性与实物校准各自保留后续关口。
- 临时设备连接、完整 Mesh NoC、Compute Tile 与 Linux 全栈验证沿用其
  独立实施关口。全局 M5/M7 的其他模块继续按原状态追踪。

## 重跑入口

```sh
make pcie-source-check
.venv/bin/python tools/pcie_spec.py --check --gate G3
make pcie-test TEST_JOBS=3 JOBS=6
```

单独运行时使用 `tools/pcie_verify.py --matrix`，或
`tools/pcie_system_verify.py --suite smoke/faults/checkpoint`。
原生线上的 CRC、相位、信用和完成匹配可用
`tools/pcie_verify.py --native <用例输出目录>` 重新核对。
