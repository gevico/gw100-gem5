# 阅读笔记：跨厂商评测

| 项目 | 内容 |
|---|---|
| 相关性 | ★★ 提供第三方实测和规格汇总，可用来交叉核对厂商口径 |
| 对应文档 | DOC-001 / 002，SPEC-220 / 330 |

## 资料与引用代号

表中"出处"一列写作"代号 p.页码"，页码对应 PDF 页，也对应同名 `.md` 里的 `<!-- p.N -->` 标记。

| 代号 | 资料 |
|---|---|
| EVAL | [2026-arxiv-npu-hardware-evaluation/](2026-arxiv-npu-hardware-evaluation/)：NPU Hardware Evaluation v1.0（Covision Lab），10 款端侧加速器 |
| SUST | [2026-arxiv-llm-edge-sustained-load/](2026-arxiv-llm-edge-sustained-load/)：LLM Inference at the Edge，持续负载下的 LLM 实测 |

## EVAL：10 款端侧加速器评测

**覆盖范围与现状**
- ASIC NPU：Hailo-8、Hailo-10H、Axelera Metis、Axelera Europa、EdgeCortix Sakura II；SoC DSP：SiMa MLSoC、Qualcomm QCS6490 / QCS8550；集成 NPU：Intel Lunar Lake、AMD XDNA2。基准是 NVIDIA RTX A5000 加 TensorRT（p.1）。
- **Hailo-10H、XDNA2 和 Lunar Lake 尚未实测**，Europa 还拿不到硬件（p.4–5）。所以这一版对我们最关心的三颗芯片只有规格汇总，没有实测数据。
- 12 个参考模型以 CNN 和视觉 Transformer 为主，不含 LLM（p.3）。

**有用的结论**

| 结论 | 出处 |
|---|---|
| Hailo-8：多路并发吞吐出色，SDK 好用，但**算子支持有限，12 个模型中有 4 个跑不了** | p.2 |
| Axelera Metis：峰值算力高，多芯片扩展接近线性，**算子覆盖的局限和 Hailo-8 相同** | p.2 |
| 只要模型的算子在 ASIC NPU 的支持范围内，Hailo-8 和 Metis 的每瓦性能最好 | p.2 |
| 实际落地时，峰值算力很少是瓶颈；关键在模型准备（量化、稀疏、剪枝）以及它和硬件的配合程度 | p.2–3 |
| 没有专门硬件支持的稀疏化基本没有收益 | p.13 |

**规格汇总（摘录）**

| 芯片 | 规格 | 出处 |
|---|---|---|
| Hailo-10H | 40 TOPS（INT4）/ 20 TOPS（INT8），功耗低于 3.5 W，8 GB LPDDR4，PCIe Gen3 x4；有 M.2 和半高 PCIe 卡两种形态 | p.4、p.7 |
| Axelera Europa | 8 个第二代 AIPU 核，16 个 RISC-V 向量核，128 MB L2，片外带宽 200 GB/s | p.4 |
| AMD XDNA2 | 32 个 AI Engine Tile，50 TOPS（INT8）；块浮点 BF16 以 INT8 的吞吐提供 16 位浮点精度；片外 LPDDR5 约 120 GB/s，片上 SRAM 约 800 GB/s | p.5 |

## SUST：持续负载下的 LLM 实测

**测试设置**（p.2–4）
- 模型：Qwen2.5-1.5B，4-bit 量化。四个平台各用各的框架和量化格式。
- Hailo-10H 的平台是树莓派 5，框架是 hailo-ollama。**hailo-ollama 把模型的层拆分到 NPU 和 CPU 上执行**：注意力放在 NPU 上，Q4 权重送到 NPU 的 INT8 计算单元。也就是说，整个模型并没有完全跑在 NPU 上（p.3）。
- Hailo-10H 的 PCIe 供电线上没有电流传感器，功耗是在树莓派侧采样估算的（p.3）。

**结果**

| 平台 | 持续吞吐 | 功耗 | 每 token 能耗 | 出处 |
|---|---|---|---|---|
| 树莓派 5 + Hailo-10H | 6.91 tok/s，变异系数 0.04%（几乎没有波动） | 平均 1.87 W | 约 270 mJ | p.8、p.11 |
| iPhone 16 Pro | 发热稳定后约 23.7 tok/s | — | — | p.13 |
| RTX 4050 笔记本（电池供电） | 131.7 tok/s | 34.1 W | 约 259 mJ | p.11 |

- 论文认为 Hailo-10H 的瓶颈是模块上的内存带宽，6.9 tok/s 远低于 40 TOPS 峰值所对应的水平，这是自回归 decode 的特性决定的（p.1、p.8）。

## 对我们的启示

| 我们的设计问题 | 借鉴点 | 对应文档 |
|---|---|---|
| 算子覆盖 | 固定功能的数据流 NPU 最大的落地障碍是算子不支持。我们必须保证有通用的兜底路径：可编程向量单元，加上 cluster 或管理 Tile 上的 CPU | SPEC-220 / 330 |
| 整模型留在设备上 | Hailo-10H 的 LLM 方案要在 NPU 和 CPU 之间拆分层，每个 token 都要往返 PCIe。我们的目标是整个模型都在设备上执行 | SPEC-400，DOC-001 |
| 结果的稳定性 | Hailo-10H 的吞吐几乎没有波动，这是数据流架构的天然优势，可以作为产品卖点写进 DOC-001 | DOC-001 |
| 功耗测量 | 第三方测不到加速卡本身的功耗。我们应在卡上提供功耗遥测，供驱动读取 | SPEC-330 / 350 |

## 存疑与待核对

- SUST 里四个平台的框架和量化格式都不相同，横向对比只能看量级。
- SUST 对 Hailo-10H 的功耗是在树莓派侧估算的，可能偏低。
