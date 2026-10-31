# 文档分类

本目录存放 GW100 的交付文档。本 README 只说明文档怎么分类、怎么命名。

## 1. 按性质分目录

| 类别 | ID 前缀 | 目录 | 性质 |
|---|---|---|---|
| 规格 | `spec-` | `spec/<领域>/` | **规范性**：定义硬件"必须怎样"，gem5 模型、驱动、编译器（以及将来的 RTL）都以此为准 |
| 手册 | `man-` | `man/` | **使用性**：面向具体读者讲"怎么用"，引用规格，不重复定义 |
| 决策记录 | `adr-` | `adr/` | 每个重要的架构取舍记一条，写明背景、备选方案和结论 |

规格与手册冲突时，以规格为准。手册只引用规格章节，不复述定义。

规格之间可以相互引用：

- 引用写文档 ID 加章节号，例如"见 `spec-pcie` §6.4"。
- 每一项定义只在一份规格中给出，其他规格引用它，不复述。两份规格出现冲突，以给出定义的那份为准，另一份必须尽快改正。
- 调整章节编号或删除章节时，同步修改引用它的文档。

## 2. 规格按产品领域分子目录

按硬件模块的边界划分，看这块功能将来在 RTL 里属于哪个模块；横跨全芯片的归"系统"。读者是谁不作为分类依据。

| 领域 | 目录 | 范围 | 文档 |
|---|---|---|---|
| 系统 | `spec/system/` | 横跨全芯片：总体结构、地址、时钟复位、错误、可观测性 | `spec-arch`、`spec-address-map`、`spec-clock-reset-power`、`spec-interrupt-ras`、`spec-debug-perf` |
| 计算 | `spec/compute/` | Tile 内部怎么算：功能单元、指令、数值；片上 SRAM 也在这里 | `spec-compute-tile`、`spec-matrix-engine`、`spec-vector-engine`、`spec-isa`、`spec-numerics` |
| 互连 | `spec/interconnect/` | 数据和同步怎么在 Tile 之间流动 | `spec-noc`、`spec-tile-sync` |
| 存储 | `spec/memory/` | 片外 DRAM 子系统 | `spec-dram` |
| 主机接口 | `spec/host/` | 芯片与 host 之间：链路、DMA、固件、命令协议、程序格式 | `spec-pcie`、`spec-host-dma`、`spec-mgmt-tile`、`spec-hdi`、`spec-exe-format` |

手册不分子目录：`man-registers`、`man-programming`、`man-perf-tuning`、`man-driver-guide`、`man-gem5-user`、`man-gem5-dev`。

## 3. 文档 ID

- **ID 是 `<类别>-<主题>`**，全小写、连字符分隔、只用 ASCII，例如 `spec-pcie`、`man-driver-guide`。看 ID 就知道内容。
- **同一个 ID 用在所有地方**：文件名（`spec/host/spec-pcie.md`）、文档标题（`# PCIe 子系统规格（spec-pcie）`）、源文件头的 `@see`、提交 scope、文档之间的引用。
- **ID 不含领域**。文档调整领域时只移动目录，ID 和引用都不变。
- **ADR 带序号**：决策记录按时间排列，文件名 `adr-NNN-<主题>.md`，ID 和提交 scope 用 `adr-NNN`。
- **版本不进文件名**，写在文档头里。
- **ID 定下就不改**。文档拆分时取新 ID，旧 ID 标"已废止"，不再复用。新增文档先在 `tools/git-hooks/scopes.txt` 登记。
