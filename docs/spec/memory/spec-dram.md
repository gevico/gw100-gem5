# Memory Tile 与 LPDDR5X 规格（spec-dram）

> 当前交付范围按 2026-11-06 的最新决定收敛为通用 LPDDR5-8433
> 性能预设，定义见[通用性能附录](spec-dram/lpddr5-8433.md)。
> 以下保留前期型号对齐的调研记录，其资料关口用于型号专属数字模型。

| 项 | 内容 |
|---|---|
| 版本 | v0.0 |
| 状态 | 资料草稿（Draft）：只有参数来源表、候选工作点和缺口，尚无规范性定义；存在 M5 阻塞（§4） |
| 负责 | 架构线 |
| 读者 | gem5 线、固件、将来的 RTL 团队 |

---

## 1. 范围

本规格将定义 Memory Tile（DRAM 控制器、受保护区域检查、训练接口）和设备内存的器件组织、时序参数、刷新、调度与写可见性（`spec-arch` §3.2、§3.3）。当前版本只完成第一步：为每个将要写入本规格的参数找到原始依据，并如实标明核对状态；没有依据的参数不填数值。

参数来源表的"核对状态"一列取以下值：

| 状态 | 含义 |
|---|---|
| 已核对 | 已回原始资料的原页核对（PDF 渲染后逐项比对）；资料尚未收入 `reference/`，核对范围见"来源"一列 |
| 推算 | 由已核对的数值按"推导"一列计算得出，不是资料中的原值 |
| 上游线索 | 只在上游模型（例如 gem5 的 `DRAMInterface`）中出现；不宣称已核对 JEDEC 或厂商资料，不作为规范性取值 |
| 无依据 | 本轮没有取得可核对的原始资料；不填默认值，列为 §4 的阻塞 |

## 2. 候选器件与工作点

- 器件类型：LPDDR5X，速度档 8533 Mb/s/pin（`spec-arch` §3.3 的 `dram.type`）。Micron 的 LPDDR5X 数据手册把这一档标为 `-023`，WCK:CK = 4:1（S1 p.1、p.2 Table 1）。
- 由此推算：CK = 8533 ÷ 8 ≈ 1066.625 MHz，WCK ≈ 4266.5 MHz（与 `spec-clock-reset-power` §3.2 的参考值一致）；每条 x16 通道的峰值数据带宽 2 B × 8.533 × 10⁹ ≈ 17.07 GB/s，8 条通道约 136.5 GB/s（推算，引脚上的理想值，与 `spec-arch` §3.3 一致）。
- 组织：S1 列出每个 die 两条 x16 通道，支持 Bank Group 模式（4 个 bank group × 4 个 bank）与 16-bank 模式，突发长度 16 或 32（S1 p.1、p.8 Table 4）。S1 的 Features 页没有给出速度档与 bank 模式的适用关系；在 8533 Mb/s 下可用哪些模式待原始资料确认（G4）。

## 3. 参数来源表

| 参数 | 取值 | 单位 | 器件与工作点 | 条件 | 来源 | 推导 | 核对状态 |
|---|---|---|---|---|---|---|---|
| 速度档 | 8533 | Mb/s/pin | Micron Y52Q（MT62F512M32D1DS-023 等） | WCK:CK = 4:1 | S1 p.1 Features、p.2 Table 1 | — | 已核对 |
| 每通道密度 | 8 | Gb | Y52Q | x16 通道；x32 封装每通道 1 个 rank，x64 封装为 rank 0 | S1 p.8 Tables 2–4 | — | 已核对 |
| bank 组织 | 4 BG × 4 bank（BG 模式）；16 bank（16B 模式） | — | Y52Q | — | S1 p.8 Table 4 | — | 已核对 |
| 每 bank 行数 | 32,768 | 行 | Y52Q，8Gb/通道 | — | S1 p.8 Table 4 | — | 已核对 |
| 页大小 | 2048 | B | Y52Q，BG 与 16B 模式 | — | S1 p.8 Table 4 | — | 已核对 |
| 原生突发长度 | 16 | beat | Y52Q | — | S1 p.8 Table 4 | — | 已核对 |
| 突发起始地址边界 | 128 | bit | Y52Q | — | S1 p.8 Table 4 | — | 已核对 |
| tRFCab | 210 | ns | Y52Q；原表列头为"8Gb Die"（注 a） | BG 与 16B 模式 | S1 p.9 Table 5 | — | 已核对（数值） |
| tRFCpb | 120 | ns | 同上（注 a） | BG 与 16B 模式 | S1 p.9 Table 5 | — | 已核对（数值） |
| tPBR2PBR | 90 | ns | 同上（注 a） | 不同 bank | S1 p.9 Table 5 | — | 已核对（数值） |
| tPBR2ACT | 7.5 | ns | 同上（注 a） | 不同 bank | S1 p.9 Table 5 | — | 已核对（数值） |
| tRFCab | 280 | ns | Micron Y4BM，12Gb die | BG、16B、8B 模式 | S2 p.9 Table 4 | — | 已核对 |
| tRFCpb | 140 | ns | Y4BM，12Gb die | BG、16B、8B 模式 | S2 p.9 Table 4 | — | 已核对 |
| tPBR2PBR | 90 | ns | Y4BM，12Gb die | 不同 bank | S2 p.9 Table 4 | — | 已核对 |
| tPBR2ACT | 7.5（BG、16B）；10（8B） | ns | Y4BM，12Gb die | 不同 bank | S2 p.9 Table 4 | — | 已核对 |
| 每 bank 行数 | 49,152（x16 die）；98,304（x8 die） | 行 | Y4BM，12Gb die | — | S2 p.9 Table 3（续） | — | 已核对 |
| 每通道峰值带宽 | 17.07 | GB/s | 8533 Mb/s，x16 | 引脚理想值 | — | 2 B × 8.533 × 10⁹ | 推算 |
| 每传输 64 B 的数据总线占用下限 | 3.75 | ns | 同上 | 不计命令、切换与开销 | — | 64 B ÷ 17.066 GB/s | 推算 |
| tRCD、tRPpb、tRPab、tRAS、tRC | — | ns | LPDDR5X-8533 | — | 厂商的通用规格（S1 p.2 列出的 Micron "General LPDDR5/LPDDR5X Specifications 2、3"）或 JESD209-5C；本轮均未取得可核对的原文 | — | 无依据（G1） |
| RL、WL、nWR、tCCD、tRRD、tFAW、tWR、tRTP、tWTR | — | CK 或 ns | LPDDR5X-8533 | 随速度档变化 | 同上 | — | 无依据（G1） |
| tREFI、tREFW、刷新的推迟上限 | — | — | LPDDR5X | 随温度变化 | 同上（S1、S2 只给出与密度相关的刷新参数，见各表的注 1） | — | 无依据（G1） |
| LPDDR5-5500 的同类参数 | 例：tRCD 18 ns、tRP 18 ns、tRAS 42 ns、tRFC 210 ns、tREFI 3.9 µs | — | LPDDR5，5500 Mb/s，8 Gb/通道 | gem5 注释称"based on initial JEDEC specification" | S3 的 `LPDDR5_5500_1x16_BG_BL32` | — | 上游线索 |
| bank 模式与速度的关系 | 注释：8B 模式 BL32、各频率；16B 模式 BL32 或 BL16、≤ 3.2 Gb/s；带 bank group 的 16B 模式 > 3.2 Gb/s | — | LPDDR5 | — | S3 的同一段注释 | — | 上游线索（不推广到 LPDDR5X-8533） |

注 a：S1 p.1 写每个 die 两条 x16 通道，p.8 Table 4 写每通道 8 Gb，p.9 Table 5 的列头写"8Gb Die"；这几行适用的密度层级待确认，数值本身与原页一致。

来源：

| 代号 | 文档 | 版本 | URL | 获取 |
|---|---|---|---|---|
| S1 | Micron, Production Data Sheet: 315b, 441b, 561b, x32, x64 LPDDR5X SDRAM (Y52Q) | Rev. G 05/2025 | <https://mm.digikey.com/Volume0/opasdata/d220001/medias/docus/8885/315b-441b-561b-y52q-sdp-ddp-lpddr5x.pdf> | 2026-11-04，未收入 `reference/` |
| S2 | Micron, Y4BM LPDDR5/LPDDR5X SDRAM（315b、441b，DDP/QDP/8DP，non-auto） | Rev. D 08/2022 | <https://www.farnell.com/datasheets/3917539.pdf> | 2026-11-04，未收入 `reference/` |
| S3 | gem5 `src/mem/DRAMInterface.py` | gem5 子模块提交 `f5c5a6e390f55dd5984977815bf9d0bd05da6945` | — | 只作线索 |

## 4. 缺口与阻塞

| # | 缺口 | 阻塞的对象 | 截止 | 说明 |
|---|---|---|---|---|
| G1 | LPDDR5X-8533 的核心时序（tRCD、tRP、tRAS、tRC、RL/WL、tCCD、tRRD、tFAW、tWR、tRTP、tWTR）与刷新间隔（tREFI 等）没有可核对的原始资料 | 本规格的时序参数表；M7 的控制器参数；`spec-noc` 草稿 §2–§4 中目标服务时间的最坏预算 | 11-07（M5 关口） | 公开取得的厂商数据手册只给出与密度相关的参数，并引用本轮未取得的通用规格；JESD209-5C 需购买或会员权限。处理方案见 §5 D1 |
| G2 | 32 GiB、8 条 x16 通道要求每通道 4 GiB（32 Gb，推算）。已核对的组织：Y52Q 每通道 8 Gb、x64 封装只有 rank 0（S1 p.8 Tables 2–4）；Y4BM 用 x16 die 时每通道最多 2 个 rank、24 Gb，用 x8 die 拼成 x16 时每通道 2 个 rank、4 颗 die、48 Gb（推算，S2 p.8 Tables 1–3） | `spec-arch` 的 `dram.size` 与本规格的器件组织、rank 数（rank 数又决定刷新期间其他 rank 能否服务） | 11-07 | 目前没有落实一个恰好每通道 32 Gb 的组织；这不等于资料证明不可行。物理容量多于 32 GiB、只开放 32 GiB 是另一个待决方案，不视为已解决 |
| G3 | 训练寄存器接口与固件步骤（物理过程可简化已获批准，接口和步骤须真实） | M7 的训练与 `FW_READY` 门控 | 11-07 | 待写；依据 CRC 的复位释放顺序（`spec-clock-reset-power` §4.3）与固件初始化流程（`spec-mgmt-tile` §8.5） |
| G4 | 8533 Mb/s 下可用的 bank 模式与突发长度 | 通道内的 rank、bank、行、列映射与调度（通道之间的交织已由 `spec-address-map` §3.2 定义） | 11-07 | S1 只列出支持的模式；上游线索针对 LPDDR5，不推广 |

## 5. 待决事项

| # | 事项 | 可选方案 |
|---|---|---|
| D1 | G1：LPDDR5X-8533 核心时序的依据 | (a) 取得 JESD209-5C 或厂商通用规格（购买、会员或厂商授权），收入 `reference/` 后核对；(b) 换用有公开原始资料的 LPDDR5 家族速度档（Goal 允许"具体速率不限"），同样需要可核对的原文，选定后同步修改 `spec-arch` 的 `dram.type`、带宽推算及相关时钟参考值，并重新计算 NoC 预算；(c) 维持阻塞，M5 不关闭 `spec-dram` |
| D2 | G2：32 GiB 的组织 | (a) 找到并核对每通道 32 Gb 的器件组织；(b) 物理容量多于 32 GiB，只开放 32 GiB；(c) 修改 `dram.size` 或通道数（属于修改 `spec-arch` 的既定参数，需用户决定） |

本轮不修改 `reference/` 子模块的指针；资料是否收入 `reference/`（`gevico/gw100-references`）由用户决定。
