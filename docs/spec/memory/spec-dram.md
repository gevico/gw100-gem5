# Memory Tile 与通用 LPDDR5-8433 规格（spec-dram）

| 项 | 内容 |
|---|---|
| 版本 | v0.2 |
| 状态 | 实施基线，运行验收随提交关口记录 |
| 负责 | 架构线 |
| 读者 | 模型、固件、驱动与 RTL 团队 |

## 1. 范围与参数归属

本规格定义 GW100 的四个 Memory Tile、八通道 32 GiB 通用内存系统。
器件运行期时序使用 gem5 的 MemCtrl/DRAMInterface，控制器级初始化由真实
RISC-V 固件执行。训练反馈以可配置时延、成功、失败和超时提供，能力寄存器
标明这一抽象。训练时延为 GW100 建模假设。

器件时序由 LPDDR5.py 拥有，推算见[性能附录](spec-dram/lpddr5-8433.md)。
系统拓扑、覆盖参数、寄存器和状态编码由 spec-src/dram/gw100-memory.json
拥有，tools/memory_spec.py 生成 C、C++、Python 定义。生成检查必须通过。
前期型号资料保存于[来源审计](spec-dram/sk-hynix-research.md)。

临时连接适配器为 32 B、1 GHz，并分别统计传输、排队和 CDC 时间。
本轮验收采用器件时序抽象、控制器功能与固件生命周期三个维度。

NoC G0 的完整 Mesh 档案采用 64 B 含报头链路和 128 B 源端完整包 CDC，
接入关口见 `spec-noc`。DDR 边界仍引用本文的每通道 32 B FIFO 参数。
NoC 只选择 Memory Tile 并传原始 DPA，DRAMInterface 压缩一次通道位。
目标服务从 TARGET_ACCEPT 计到 TARGET_COMPLETE，排队、CDC、目标服务和
返回等待分别观测。原生 FR-FCFS 的最坏服务上界保持未证明。

## 2. 拓扑、时序与地址

| 参数 | 值与依据 |
|---|---|
| 组织 | 四 Tile，每 Tile 两条 x16 通道，每通道两个 16 Gb rank |
| 容量 | 每器件 2 GiB，每通道 4 GiB，合计 32 GiB，组织推算 |
| bank | 每 rank 四个 bank group，每组四 bank，2 KiB 行 |
| burst | BL32，每次原生传输 64 B |
| 工作点 | 8433 MT/s，WCK:CK=4:1，全局 tick 为 1 ps |
| CK | 精确推算为 8000/8433 ns，量化为 949 ps |
| 阵列延迟 | 沿用通用预设，tCL=23.28 ns，tCWL=tCL |
| 刷新 | tRFC=280 ns，tXS=287.5 ns，tREFI=3.9 µs |
| 环境 | 常温 25°C |

刷新参考 SK1 PDF p.295 Table 240 的 16 Gb、Enhanced DVFSC 关闭、
Tcase≤85°C 档，已核对原页。表列 tREFI 为 3.906 µs，模型采用上游的
3.9 µs。tXS 按上游 tRFC+7.5 ns 的关系推算。这些值为通用档案的参考选择。

DPA 和 Tile 窗口由 spec-address-map 拥有。通道号为 DPA[10:8]，
Tile 编号为通道号右移一位，256 B 交织。原始 DPA 传给交织范围，
DRAMInterface 处理一次通道位压缩，通道内部采用 RoRaBaCoCh。
通道内局部地址的 bank 位为 [14:11]，rank 位为 [15]，row 位为 [31:16]。
每 rank 有 65536 行，由容量、bank 与行缓冲推算。

## 3. 数据通路、资源与完成

设备内存属于独立 timing System，存储按需分配。管理 CPU、HDMA 和 BAR2
分别通过硬件绑定的入口发起请求。来源编码为 MGMT=0、HDMA=1、BAR2=2。
连接适配器把请求者编号转换到设备 System，保留原请求、事务号与复位代次。

| 资源 | 上限或默认策略 |
|---|---|
| 每来源在途父事务 | 16，含返回反压，与 NoC 的 64 B 子请求槽分开计量 |
| 每通道前端槽 | 64 个 64 B 事务，含读改写与返回占用 |
| 每通道原生队列 | 64 读 burst、64 写 burst |
| CDC | 请求、响应各八个 32 B 拍，指针同步两级 |
| 控制器流水 | 前端、后端各两个实际 DDR 控制器周期 |
| 调度 | 原生 FR-FCFS、close_adaptive |
| 初始化 | 每通道单在途命令 |

完整 Mesh 的 HDMA 前端预留 64 个子请求槽，管理和 BAR2 各 16，Compute
各 16。当前 HDMA 默认 devChunk=64 B，现有 16 个父事务只能填充 16 个
此类子请求，G2 接入时单独验收其并行度。原生队列及前端资源继续由本文拥有。

NoC 接入后的保护传播采用每 Tile 四项区域的 64 B 快照，通过控制消息送往
18 个请求者物理 NI。ENABLE 等待全部成功确认后完成，目标继续检查权限。
初始化自检和普通数据准入使用独立状态。快照版本、失败和复位规则见
`spec-noc` §3.2、§4，此协议由完整 Mesh 接入关口验证。

入口按 64 B burst 和 256 B 通道边界拆分请求，首先检查完整请求的地址和
权限。拒绝发生在数据执行之前。不足一个 burst 的写执行原生读改写。
同 burst 的重叠请求保持顺序，支持自然对齐 1、2、4、8 B 的不可撕裂访问。

普通写完成采用原生写缓冲与转发语义。fence 等待此前请求完成并保证后续
合法读取可见。媒体排空还要等待命令队列、数据事件、写恢复与预充电。
自刷新、复位和 checkpoint 必须完成媒体排空。

队列满返回反压并通过 retry 恢复。已接受请求恰好完成一次，响应保持到
接收方接受。入口在复位或电源切换时关闭，新代次启用前收尾旧代次。
执行中故障报告效果可能不完整，错误快照保存其来源和阶段。

## 4. 软件寄存器

每 Tile 的控制块位于既有 Tile 窗口寄存器区，起始偏移 0x800000。
控制块大小为 0x3000。寄存器采用自然对齐的 32 位或 64 位访问，
具体宽度如下。保留位置读零、写忽略，宽度错误返回访问错误。

| 公共偏移 | 名称 | 宽度/访问 | 含义 |
|---|---|---|---|
| 0x000 | VERSION | 32/RO | 0x00010000 |
| 0x004 | TOPOLOGY | 32/RO | Tile[7:0]、通道数[15:8]、rank 数[23:16] |
| 0x008 | STATUS | 32/RO | 两通道 READY 位[1:0]、复位中[8] |
| 0x00c | CAPABILITIES | 32/RO | [0] 控制器级训练、[1] 自刷新、[2] 部分写 RMW |
| 0x010 | EPOCH | 64/RO | 设备内存复位代次 |
| 0x018 | ERROR | 32/W1C | 错误码对应的位图 |
| 0x01c | ERROR_SOURCE | 32/RO | 首个未清除错误的来源 |
| 0x020 | ERROR_ADDR | 64/RO | 错误 DPA |
| 0x028 | ERROR_TXN | 64/RO | 事务号 |
| 0x030 | ERROR_INFO | 64/RO | 通道[7:0]、rank[15:8]、阶段[23:16] |
| 0x038 | PROTECTION_LOCK | 32/W1S | [0] 锁定四项区域表 |
| 0x03c | PERF_FREEZE | 32/RW | [0] 冻结计数器读取快照 |
| 0x040–0x070 | 基础计数器 | 64/RO | 请求、读字节、写字节、retry、CDC、排队、传输 tick |

计数器在仿真内持续累积，内存复位通过 EPOCH 区分代次。CDC 与传输 tick
按 FIFO 拍累加，不能与排队 tick 相加作为单个事务的端到端延迟。
入口统计接受的逻辑事务，Tile 统计通过预检的数据子请求。媒体实际
burst 字节及 RMW 流量由原生 MemCtrl 统计，排队计数覆盖请求首拍前的等待。

四项保护表从 0x100 开始，跨度 0x20。每项 BASE、LIMIT 为偏移 0、8 的
64 位 RW 值，ENABLE 为偏移 0x10 的 32 位 RW 值。区间为 [BASE,LIMIT)，
启用项按 4 KiB 对齐且包含于设备内存。锁定写入先验证全部条目。
只有管理 CPU 可访问控制寄存器，锁定后配置保持到内存复位。
正常固件锁定空表。HDI 顶端 1 MiB 暂存区按 spec-hdi §6.7 供 HDMA 使用。

两个通道块从 0x1000、0x2000 开始，跨度 0x1000。

| 通道偏移 | 名称 | 宽度/访问 | 含义 |
|---|---|---|---|
| 0x00 | PROFILE | 32/RW | 通用工作点编号 1 |
| 0x04 | COMMAND | 32/WO | 下表命令 |
| 0x08 | STATE | 32/RO | 下表状态 |
| 0x0c | TRAIN_MASK | 32/RO | 当前校准或训练阶段中两 rank 的成功位 |
| 0x10 | ERROR | 32/RO | 最近命令错误，rank[15:8] |
| 0x18 | SEQUENCE | 64/RO | 成功或失败的命令完成次数 |
| 0x20 | CK_PS | 64/RO | 量化后的 CK 周期 |

| 命令 | 编码 | 前提与完成状态 |
|---|---|---|
| CONFIGURE | 1 | 已释放、PLL 锁定、PROFILE=1，进入 CONFIGURED |
| CALIBRATE | 2 | CONFIGURED，等待 10 µs，进入 CALIBRATED |
| TRAIN | 3 | CALIBRATED，等待 100 µs，两 rank 并行，进入 TEST |
| ENABLE | 4 | TEST、保护已锁定，进入 READY |
| QUIESCE | 5 | TEST 或 READY，入口关闭并完成媒体排空，进入 QUIESCED |
| SELF_REFRESH | 6 | QUIESCED，完成额外刷新与自刷新进入，进入 SELF_REFRESH |
| RESUME | 7 | QUIESCED 或 SELF_REFRESH，等待媒体恢复，返回先前 TEST/READY |

RESET=0、CONFIGURED=1、CALIBRATING=2、CALIBRATED=3、TRAINING=4、
TEST=5、READY=6、QUIESCING=7、QUIESCED=8、SELF_REFRESH=9、
RESUMING=10、FAILED=15。忙时提交新命令返回 BUSY，原操作继续。
TEST 只允许管理 CPU 数据访问。训练失败进入 FAILED，超时由固件记录。
错误编码与共享档案一致。故障注入可选择通道、rank、阶段及失败或无反馈，
只能改变训练反馈或设备响应。

进入 QUIESCE 时关闭新事务的准入。已通过整请求预检的事务保留通道信用，
全部子 burst 和上游返回完成后再排空原生控制器，随后报告 QUIESCED。

## 5. 时钟、复位与电源

CRC 的 NoC 释放控制临时适配器，未释放时管理 NI 返回访问错误。
DDR PLL 配置的 EN 为位 0，工作点编号为 [3:1]，默认有效配置为 3。
PLL 锁定、Memory Tile 复位撤销、配置与训练成功后，媒体时序开始运行。
控制面在已释放的互连上可读取复位状态，数据面按通道状态门控。

冷复位、热复位和设备完全复位共用入口。先关闭入口并收尾旧代次，
重置控制器状态、保护表和训练结果，再允许新一轮固件初始化。
FLR 保留内存配置和数据，只收尾 PCIe 来源旧事务。

D3hot 时硬件停止新业务并发布 PM 事件。固件排空全部通道，进入自刷新。
D0 恢复时执行退出与 tXS 等待，然后按 FLR 的范围恢复 PCIe 功能。
初始化期间收到 D3hot 时，完成有界初始化后进入保持状态。
初始化中 FLR 只做清理和确认，就绪发布由启动状态统一控制。

checkpoint 采用排空的内存子系统状态，保存寄存器、代次、数据和保持状态。
恢复时重建必要媒体时序并满足自刷新退出等待。

## 6. 固件与验证关口

fw_main 入口发布 STAGE=4、FW_READY=0。建立计时、看门狗与事件服务，
释放连接路径、配置 PLL_DDR、释放四 Tile，配置并训练八通道。
每通道每 rank 的首末 cache line 通过 timing 路径执行两种模式自检，
排空写入，回读并锁定保护表，开放数据，再初始化 HDMA/HDI 并发布就绪。

阶段默认超时 1 ms，等待期间喂狗和处理 FLR/PM。失败快照包含 Tile、
通道、rank、阶段与原因，发布 STAGE=0xF、FW_READY=0。
全部精确配置固件变体共用初始化驱动。旧 atomic 配置使用命名兼容配置
和匹配镜像，测试矩阵记录能力要求。

验收覆盖全部通道和 rank 的译码与数据、部分写、保护、反压、初始化失败、
复位代次、D3/D0、排空和 checkpoint，以及原有裸机与 GTest 回归。
单通道手算核对流水、CDC 和行命中延迟。训练替身、原生器件时序近似与
临时互连分别列入理想化清单，证据只关闭实际完成的子项。
