# 阅读笔记：Axelera Metis / Europa

| 项目 | 内容 |
|---|---|
| 相关性 | ★★ 端侧 PCIe/M.2 竞品；"从视觉转向 LLM 需要把 DRAM 带宽提高 6 倍"的实证 |
| 对应文档 | SPEC-100 / 210 / 240 / 330，DOC-001 |

## 资料与引用代号

表中"出处"一列写作"代号 p.页码"，页码对应 PDF 页，也对应同名 `.md` 里的 `<!-- p.N -->` 标记。四份都是讲稿，没有论文。

| 代号 | 资料 | 内容侧重 |
|---|---|---|
| EPCC | [2025-hpcasia-epcc-axelera/](2025-hpcasia-epcc-axelera/) | Metis SoC 和 AI Core 架构、规格表（**最技术化的一份**） |
| EVS | [2024-edge-ai-summit-verhoef-dimc/](2024-edge-ai-summit-verhoef-dimc/) | 数字存内计算（D-IMC）原理与 AI Core 组成 |
| ESA | [2025-esa-workshop-axelera/](2025-esa-workshop-axelera/) | 公司与产品路线：Metis、Europa、Titania |
| ESSCIRC | [2022-esscirc-eleftheriou-imc/](2022-esscirc-eleftheriou-imc/) | 存内计算综述（PCM、模拟与数字 SRAM），与产品无直接关系 |

Europa 只有新闻稿：[Axelera launches Europa AIPU](https://axelera.ai/news/axelera-launches-europa-aipu-setting-new-industry-benchmark-for-ai-accelerator-performance-power-efficiency-and-affordability)。

## 关键参数

**Metis（2024）**

| 参数 | 数值 | 出处 |
|---|---|---|
| 工艺 / 频率 | TSMC N12，0.8 GHz | EPCC p.9 |
| 峰值算力 | 210–214 TOPS（INT8）；每个 AI Core 52.4 TOPS | EPCC p.9；EVS p.7；ESA p.7 |
| 能效 | 15 TOPS/W（无稀疏，随机均匀分布的数据）；在高稀疏度、降低吞吐的条件下可达 82 TOPS/W | EPCC p.9 |
| 功耗 | 平均 5–9 W | ESA p.7 |
| AI Core 数 | 4 个，每个都能独立跑完一整个网络；最多 4 个模型并行 | EPCC p.6；EVS p.11；ESA p.7 |
| 片上存储 | 每个 AI Core 4 MiB L1；共享 32 MiB L2；全片共 52 MiB；NoC 到共享存储 1 Tbit/s | EPCC p.6–7 |
| 片外存储 | LPDDR4x，34 GB/s；卡上 2 GB 或 4 GB | EPCC p.9、p.15 |
| Host 接口 | PCIe Gen3 x4 | EPCC p.9 |
| 系统控制器 | RISC-V，跑实时操作系统：负责启动芯片、管理外设和 AI Core | EPCC p.6 |
| 安全 | 安全启动；权重与数据加密 | EPCC p.6 |

**AI Core 内部**

| 单元 | 说明 | 出处 |
|---|---|---|
| 矩阵向量乘（MVM） | 数字存内计算阵列：512 输入 × 512 输出，可存 4 组权重；INT8 输入和权重，INT32 累加；可累加多达 16k 个输入 | EVS p.8–10 |
| 数据处理单元（DPU） | 逐元素向量运算、激活函数 | EVS p.10 |
| 深度卷积单元（DWPU） | 深度卷积、池化、上采样 | EVS p.10 |
| 权重解压单元 | 在线解压权重 | EPCC p.7 |
| 控制 | 一个 RISC-V 控制核驱动数据流引擎 | EPCC p.7–8 |
| 数据通路 | 两条可并行的流式数据通路，MVM 一条，DWPU 一条 | EPCC p.8 |
| 后台加载权重 | 利用多组权重：算当前这组时，加载下一次运算要用的权重 | EPCC p.8 |
| 低利用率时的能效 | 细粒度的活动门控和时钟门控 | EVS p.9 |

**Europa（2025-10 发布，计划 2026 年出货）**

| 参数 | 数值 | 出处 |
|---|---|---|
| 算力与功耗 | 629 TOPS（INT8），45 W | ESA p.5、p.7 |
| 计算核 | 8 个第二代 AI Core（存内计算加大型向量引擎） | 新闻稿 |
| 预处理与后处理 | 两组各 8 个 RISC-V 向量核 | 新闻稿 |
| 存储 | 128 MB L2 SRAM；256-bit LPDDR5，200 GB/s | 新闻稿 |
| 其他 | PCIe；集成视频解码；宣称可跑 80B 参数以内的 LLM | ESA p.7 |

## 对我们的启示

| 我们的设计问题 | 借鉴点 | 对应文档 |
|---|---|---|
| 是否走存内计算 | 存内计算的收益来自权重常驻、反复使用（CNN）；LLM decode 每个权重只用一次，收益基本消失。**不建议采用** | SPEC-210 |
| 权重输入通路 | 后台加载下一组权重（双缓冲）、在线解压 | SPEC-210 / 240 |
| DRAM 带宽与产品定位 | 视觉用的 Metis 只有 34 GB/s，转向生成式 AI 的 Europa 提到 200 GB/s、L2 提到 128 MB | DOC-001 / 002 |
| 控制层次 | 每个计算核一个 RISC-V 控制核，系统级再配一个跑 RTOS 的 RISC-V 控制器，和我们"计算 Tile 加管理 Tile"的分工一致 | SPEC-200 / 330 |
| 安全 | 安全启动，加上权重和数据加密（保护客户的模型） | SPEC-330 |
| 低利用率能效 | 活动门控和时钟门控；LLM decode 时计算单元利用率低，这一点对我们尤其重要 | SPEC-130 / 210 |

## 存疑与待核对

- 没有论文，全部来自讲稿和新闻稿，属于厂商口径。
- Europa 的 PCIe 代数和 lane 数、L1 容量、向量引擎的规模都没有公布。
- "可跑 80B 以内的 LLM"没有说明是单芯片还是多芯片，也没有给出速度。
