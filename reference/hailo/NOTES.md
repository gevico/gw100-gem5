# 阅读笔记：Hailo-10H / Hailo-8

| 项目 | 内容 |
|---|---|
| 相关性 | ★★★（PCIe 端点与驱动）／★★（产品定位）。和我们产品形态最接近的竞品：M.2 模块，PCIe 接口，板载 LPDDR |
| 对应文档 | SPEC-130 / 310 / 320 / 330 / 340，DOC-001，MAN-040 |

## 资料与引用代号

表中"出处"一列写作"代号 p.页码"，页码对应 PDF 页，也对应同名 `.md` 里的 `<!-- p.N -->` 标记。驱动源码按"文件:行号"引用。

| 代号 | 资料 |
|---|---|
| 10H | [2026-hailo10h-product-brief/](2026-hailo10h-product-brief/)：Hailo-10H 芯片产品简介 |
| M2 | [2026-hailo10h-m2-product-brief/](2026-hailo10h-m2-product-brief/)：Hailo-10H M.2 模块产品简介 |
| DS | [2026-hailo8-datasheet/](2026-hailo8-datasheet/)：Hailo-8 数据手册 Rev 1.9.2（**Hailo-10H 没有公开数据手册，只能借上一代参考**） |
| DRV | [hailort-drivers/](hailort-drivers/)：PCIe 驱动源码，master 分支只支持 Hailo-10/15 |

Hailo 文档每页都标有"Confidential and Proprietary"。这些文档取自 Hailo 官网公开下载的版本，维护者已确认可以随本仓库公开（2026-10-18），见 [../README.md](../README.md) 的使用须知。

## 关键参数

**Hailo-10H**

| 参数 | 数值 | 出处 |
|---|---|---|
| 算力 | 40 TOPS（INT4）/ 20 TOPS（INT8） | 10H p.1 |
| 功耗 | 典型 2.5 W | 10H p.1 |
| 片外内存 | LPDDR4/4X；M.2 模块上 4 或 8 GB（位宽和带宽未公布） | 10H p.1；M2 p.1 |
| 形态 | M.2 Key M（2242 / 2280）；另有芯片直贴板（COB） | M2 p.1；10H p.1 |
| Host 接口 | PCIe Gen3 x4；芯片本身也支持 USB | M2 p.1；10H p.2 |
| 温度等级 | 工业级 -40～85 °C；车规级 -40～105 °C | 10H p.1 |
| 架构描述 | "结构驱动的数据流架构"，第二代 Hailo NN Core；没有公开细节 | 10H p.1 |
| 软件 | Dataflow Compiler、HailoRT（C/C++/Python/REST）、支持 Ollama；host 可以是 x86 或 ARM，系统可以是 Linux、Windows、Android | 10H p.2 |

**Hailo-8 的 NN Core**（可以视为 Hailo-10H 的前代参考）

| 参数 | 数值 | 出处 |
|---|---|---|
| 组织 | 8 个 cluster；可以在 cluster 级关断电源；支持多个网络同时运行 | DS p.37 |
| 架构要素 | 分布式存储；可按负载动态分配的计算单元；随网络结构而适配的数据流互联 | DS p.9 |
| 精度 | 权重和激活值按层可配为 8 或 16 bit；权重还可配为 4 bit | DS p.37 |
| 预处理与后处理引擎 | NMS、双线性插值（缩放、裁剪）、reshape、softmax | DS p.37 |
| I/O 通道 | 最多 16 路输入输出通道接入 NN Core | DS p.37 |
| 算力 | 最高 26 TOPS（INT8） | DS p.37 |
| 封装 | 400 脚 HFCBGA，17 mm × 17 mm | DS p.10 |

## PCIe 端点特性（DS p.38–39）

**可以直接当作我们 SPEC-310 / 320 的需求清单。**

| 类别 | 特性 |
|---|---|
| 链路 | Gen3，x1 / x2 / x4；支持 lane 反转；单物理功能、单虚功能、单虚通道 |
| 事务 | 最大载荷 256 B；最多 32 个未完成的读请求；满带宽时读 host 内存的延迟最高 2.5 µs；支持 TLP 乱序到达（内置重排缓冲区）；重传缓冲区和接收缓冲区各 2 KB |
| 中断 | MSI，最多 32 个；也支持传统 INTx |
| 可靠性 | AER；ECRC 生成与检查；数据和控制通路上有检测瞬时与永久故障的安全机制 |
| DMA | 内部 32 个通道；scatter-gather 可动态配置，每一页都能单独配置中断和起止条件；另有 16 对低延迟的流式收发通道 |
| 功能 | 可以经 PCIe 切换核心上下文（包括配置和数据）；可以从 PCIe 加载固件；有控制协议 |
| 电源管理 | ASPM L0s、L1、L1.1、L1.2；CLKREQ；LTR；D0 / D3 |

## 启动、MCU 与安全（DS p.36、p.40–42）

| 主题 | 内容 | 出处 |
|---|---|---|
| 启动方式 | 由一个引脚 IO_BS0 选择：上拉为 PCIe 启动（片内 ROM 配合 host 驱动，从 PCIe 拉取固件），下拉为 Flash 启动（ROM 从 SPI Flash 加载固件） | DS p.36 |
| MCU | 两个 Cortex-M4（200 MHz，带 MPU 和 FPU），只给固件用；两核之间用 mailbox 和信号量通信 | DS p.40 |
| MCU 存储 | 640 KB 带 ECC 的 SRAM，128 KB ROM；外部 Flash 地址空间最大 1 Gb，支持就地执行（XIP） | DS p.40 |
| 监测 | 结温传感器（±3 °C）；两个电压监测器，共 16 个监测点 | DS p.41 |
| 安全 | 安全启动；完整的生命周期管理；用户可写的非易失哈希（作为信任根）；按生命周期控制的安全调试；真随机数发生器；AES、SHA1/2、RSA 加速；TEE；可配置的硬件防火墙，保护外设、内部 SRAM 和外部存储 | DS p.41–42 |

## 驱动源码阅读导引（DRV）

| 主题 | 位置 | 要点 |
|---|---|---|
| BAR 划分 | `common/pcie_common.h:29` | BAR0 = 配置空间，BAR2 = vDMA 寄存器，BAR4 = 固件访问窗口 |
| 地址转换 | `common/pcie_common.c:34`、`:528` | PCIe 桥里有地址转换表（ATR），把 BAR 窗口映射到芯片内部地址，作用类似 Tenstorrent 的 TLB 窗口 |
| 中断 | `common/pcie_common.c:19`、`:440` | 一个读后清零的中断状态寄存器：高 8 位是软件中断（来自固件），低位分别表示 vDMA 源通道和目的通道的中断，再从逐通道寄存器读出具体通道 |
| 固件控制协议 | `common/pcie_common.c:462` | 把请求写进 BAR4 窗口，再写一个"就绪"标志位通知固件（相当于 doorbell）；响应从 BAR4 的固定偏移读回。同一个标志寄存器还用于通知固件"驱动关闭"和"软复位" |
| 固件加载 | `common/pcie_common.c:818`、`:1133` | 分阶段加载固件文件；可以经 vDMA 批量下载，启动阶段最多用 16 个通道（`pcie_common.h:24`） |
| vDMA 描述符 | `common/vdma_common.h:49` | 16 B：页大小与控制位、地址低位与数据 ID、地址高位、剩余长度与状态（设备回写）；每个引擎 32 个通道（`:142`） |
| 传输管理 | `common/vdma_common.h`、`linux/vdma/` | 描述符列表放在 host 内存；每次传输只改动必要的描述符（最后一个写入剩余长度，第一个可配中断） |
| Linux 驱动框架 | `linux/pcie/src/pcie.c`、`fops.c`、`linux/vdma/ioctl.c` | 设备探测、字符设备接口、给用户态库 HailoRT 的 ioctl |

## 对我们的启示

| 我们的设计问题 | 借鉴点 | 对应文档 |
|---|---|---|
| PCIe 端点特性 | 以上面的特性清单为基线，逐条决定我们要不要支持、支持到什么程度 | SPEC-310 |
| BAR 与地址窗口 | 三个 BAR 分别负责配置寄存器、DMA 寄存器、固件访问窗口；地址转换表把窗口映射到内部地址 | SPEC-110 / 310 |
| Host DMA | 多通道、描述符放在 host 内存、状态回写、每页可配中断 | SPEC-320 |
| 中断 | 一个汇总的状态寄存器，区分软件中断（来自固件）和 DMA 通道中断；读后清零 | SPEC-310 / 340 |
| 固件通信 | 请求区、响应区加上就绪标志，是最简的 mailbox。我们的方案应该更完整，参考 AMD 的环形缓冲区设计（见 [../amd-xdna/NOTES.md](../amd-xdna/NOTES.md)） | SPEC-330 / 400 |
| 启动 | 同时支持 PCIe 启动和 SPI Flash 启动，用引脚选择 | SPEC-330 / 130 |
| 安全 | 安全启动、信任根、生命周期管理、安全调试、防火墙 | SPEC-330 |
| 监测 | 温度和电压监测，供驱动读取 | SPEC-330 / 350 |
| 产品定位 | 2.5 W、LPDDR4，只够跑 1–2B 模型（第三方实测见 [../cross-vendor/NOTES.md](../cross-vendor/NOTES.md)） | DOC-001 |

## 存疑与待核对

- Hailo-10H 没有公开数据手册，LPDDR 的位宽、带宽、片上 SRAM 容量都不清楚。以上 PCIe、启动、安全等细节来自 Hailo-8，Hailo-10H 未必完全相同。
- 驱动源码的寄存器偏移会随芯片型号变化，只能用来理解机制，不能当成规格。
