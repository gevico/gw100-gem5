# 共享 SRAM 验证记录

本记录对应 2026-11-08 的本地工作区，档案版本为 1，仿真精度为 1 ps。
规范见 spec-sram。源码和二进制的验收指纹保存在 build/sram 下，覆盖
当前工作区中的前置 PCIe、DRAM 改动与本轮 SRAM 改动。

## 1. 实现与配置

共享组件位于 gem5-ext/src/gw100/sram。SramModel 拥有数据、ECC、端口、
队列、调度和生命周期。Gw100Sram 提供绑定来源的 timing 端口、传输到达
等待、retry、返回反压、时钟事件、统计和文件 checkpoint。

管理 Tile 使用独立启动 ROM，CPU 和 ML 分别接共享 SRAM 的来源 0、1。
来源 2 为 NoC 预留。管理系统在 Host timing、O3 和 atomic 兼容配置中均
使用 RISC-V TimingSimpleCPU 及 timing SRAM。兼容 Host 的 DRAM、PCIe
简化沿用既有能力分类。

Compute 夹具有五个硬件来源，表示矩阵、向量、DMA、NoC 和可选控制核。
三套档案分别运行读写延迟 1、2、3 周期和 II 为 1、2 周期的组合。
实际计算引擎和 Mesh 的集成验收由相应模块拥有。

## 2. 关口与覆盖

| 关口 | 本轮落点 | 验收状态 |
|---|---|---|
| G0 | 共享 JSON、三端生成定义、组织与链接布局检查、仲裁推导 | 已通过 |
| G1 | 纯 C++ 状态机、ECC、有限资源、独立数据与周期参考 | 已通过 |
| G2 | 管理 CPU、ML、ROM、时钟切换、真实固件与异常路径 | 已通过 |
| G3 | Compute 多来源夹具、性能敏感性、文件恢复与系统回归 | 已通过 |

| 行为 | 独立检查或测试落点 |
|---|---|
| 8、16、32、64 B 宏字，两种 bank 映射，边界和掩码 | ParameterValidation、CrossWordAndBankRoundTrip、NotReadyAndRange |
| 预检与未就绪拒绝、独立实例 | NotReadyResponseDuringInitialization、IndependentInstancesAndInvalidMask |
| 读采样与写提交、自然对齐小访问、重叠 RMW | VisibilityAtCommit、MaskedReadModifyWrite、OverlappingRmwKeepsBothWrites |
| 1RW、1R1W 与同宏字碰撞 | SplitPortsAndCollision、SameWordReadWriteCollisionIsSerialized |
| 在途槽、返回反压、请求 retry、来源排空 | FiniteSlotsIncludeBlockedResponses、真实 SramProbe 夹具 |
| ECC 位序、全部单比特及双比特故障 | AllSingleAndDoubleBitFaults、独立 Python 校验矩阵 |
| CE 读纠正与 RMW 修复、完整码字覆盖 UE | CorrectedReadAndRmwRepair、FullCodewordOverwriteDuringWideRmw |
| UE 无写入效果与多字部分提交 | UncorrectableRmwHasNoEffect、PartialFaultRetainsCommittedMask |
| 饱和计数、阈值、RW1C 硬件优先、首次 UE 地址 | ManagementEccRegisters、sram_ce、CPU UE 裸机用例 |
| 逐拍清零与最后提交 | InitializationCommits、真实初始化夹具 |
| 清零提交边沿的准入顺序与残余端口 II | EdgeAdmissionFollowsInitializationCommit |
| 复位取消写与旧入口代次 | ResetCancelsPendingWrite、CancelledIngressKeepsOldEpoch、三档案复位夹具 |
| 初始化中与保持中的保留复位 | PreservingResetKeepsInitializationAndRetention |
| 时钟暂停、剩余周期、25→500 MHz | PausePreservesRemainingCycles、clock-switch 夹具 |
| Ready、Retention checkpoint 与异常恢复文件 | CheckpointReadyAndRetention、CheckpointMismatchAndTruncation、RestoreFailureKeepsOriginalState、四个跨进程场景 |
| 管理 bank 仲裁保证与等待分项 | FairnessIncludesPartialWrites、ForeignWordLockCountsAsArbitration、UncontendedWaitCounters |
| CPU 取指、读取和写入 UE | sram_ue_fetch、sram_ue、sram_ue_store |
| ML 的 UE 描述符读与 DW7 RMW 写 | sram_ml_read、sram_ml_write，分别检查 HDMA PHASE 1、2 |
| ROM→固件、HDI COPY、复位、FLR、看门狗、DRAM 初始化 | 完整裸机能力矩阵 |

ECC 单元测试对三个数据模式分别遍历 72 种单比特和 2,556 种双比特故障。
独立 Python 参考另用 99 个数据向量检查编码矩阵，核对 7,128 个单比特
纠错向量。参考代码自行生成期望数据和时序公式。

管理侧的 UE 访问使用原生 TimingSimpleCPU 新增的可选错误转换接口。
管理 Tile 安装 RISC-V fault 工厂，把错误响应转换为原因 1、5、7 的访问
错误。分片访问汇总错误后才退休。压缩指令从宏字内 +2 B 取指时，mtval
记录失败指令地址，SRAM_UE_ADDR 记录实际 ECC 码字地址。

## 3. 时序与性能口径

accept、issue、sample、commit、ready、response 为阵列事件。
transport 中的 upstream_accept 和 arrival 保留入口传输时间。
bank 仲裁、来源队列、同来源地址依赖与返回接受等待分别统计。

正常 management 的阵列上限为 32 GB/s。Compute 1RW 为 256 GB/s，
1R1W 的读写方向各为 256 GB/s。宏时序含参考 ECC 组合路径。
入口的每来源每周期一个事务及每周期一个返回进一步限制具体流量。

独立性能实验固定种子 0，预热 10 µs，测量 100 µs。57 个场景包含三套
档案、延迟与 II 敏感性、分散读、单 bank 热点、混合读写和部分写 RMW。
分散读每个来源每周期请求一个宏字，management 的该流量参考为 12 GB/s，
Compute 为 80 GB/s。单 bank 热点和混合流量按物理端口与 II 计算期望。

| 基础一周期场景 | management | Compute 1RW | Compute 1R1W |
|---|---|---|---|
| 分散宏字读 | 12 GB/s | 80 GB/s | 80 GB/s |
| 单 bank 宏字写 | 4 GB/s | 16 GB/s | 16 GB/s |
| 单 bank 混合读写 | 约 4 GB/s | 16 GB/s | 32 GB/s |
| 同 bank 字节 RMW | 0.25 GB/s | 0.5 GB/s | 1 GB/s |

参考公式包含来源、端口、II、字宽和 RMW 成本。57 个场景的最大相对偏差
为 0.004%，低于 ±1% 门槛。纯模型的确定性周期误差为零。
真实 gem5 端口矩阵独立核对发出、采样、提交、响应就绪和端口间隔，
容许误差为一 tick。基础管理 RMW 压力实测最长 bank 等待为五周期，
规格的保守推导界为六周期，均满足 16 周期条件。

25→500 MHz 的切换夹具在 RMW 读取发出后暂停 84 ns。gem5 时钟域更新
先沿旧时钟对齐，本场景恢复至首个新档边沿另等待 36 ns。独立公式核对
这两个等待及切换前后各阶段的三个有效周期，事件误差为零。
trace 的 cycle 为状态机处理的有效边沿编号，空闲期间组件可以停止安排
无操作事件。绝对时间和时钟相位使用 tick 验收。

## 4. 复现与记录

本轮验收结果如下，全部使用 accepted-inputs.json 所记录的最终源码与镜像。

| 验证 | 结果 |
|---|---|
| SRAM 参数、生成物和固件布局 | 6/6 |
| 全部 GTest | 18 个程序，222/222 用例 |
| SRAM 状态机 ASan 与 UBSan | 29/29 |
| 独立性能参考 | 57/57，最大偏差 0.004% |
| gem5 timing、恢复和时钟切换 | 26/26，核对 39,204 个稳定档事件及跨切换事件 |
| 完整裸机矩阵 | 88/88，按既有能力分类跳过 8 项 |

32 个裸机用例在 timing、O3 各运行一次，atomic 兼容配置运行 24 项。
七个 SRAM 用例在三种配置中均通过。全部执行文件指纹、配置和统计已核对，
验收开始与结束的源码和二进制一致。

在仓库根目录使用以下入口。

```sh
make gem-build firmware-build baremetal-build
make sram-spec-check
.venv/bin/python tools/sram_spec_test.py
.venv/bin/python tools/gtest.py --jobs 4
.venv/bin/python tools/sram_verify.py
.venv/bin/python tools/sram_system_verify.py --output build/sram/system-accepted
PYTHONUNBUFFERED=1 .venv/bin/python tools/bmtest.py \
  --gem5 gem5/build/GW100/gem5.opt \
  --config gem5-ext/configs/run_gw100.py \
  --build-dir build/gw100-baremetal \
  --compat-build-dir build/gw100-baremetal-compat \
  --hosts 'timing atomic o3' --jobs 4 --timeout 900 \
  --max-ticks 3000000000000 --outdir build/sram/regression-accepted
```

make sram-test 将上述组件、夹具和软件矩阵串成单一入口。
单独的 AddressSanitizer、UndefinedBehaviorSanitizer 构建用于检查状态机
越界和未定义行为。构建、工具结果与完整软件矩阵保存在 build/sram。

| 文件 | 内容 |
|---|---|
| accepted-inputs.json | 验收源码、gem5、固件和裸机 ELF 的 SHA-256 |
| independent.json | 参数、期望、实测、偏差、种子、窗口与参考驱动指纹 |
| system-accepted/results.json | 26 个 timing 与恢复场景、命令、trace 及日志指纹 |
| regression-accepted.log | 全部裸机配置的逐项执行与能力跳过 |
| gtest-accepted.log | 全部 GTest 程序与用例汇总 |
| sanitize-accepted.log | SRAM 状态机的 ASan、UBSan 结果 |
| closure.json | 最终关口、结果与验收输入核对 |

源码状态使用 HEAD 与工作区文件指纹共同标识。验收后的输入再次取指纹
比较，结果与生成物一致性检查一起保存。Ready 与 Retention 的组件恢复
保存 SRAM 数据、ECC、控制和仲裁状态，整机一致性恢复由整机关口验收。

## 5. 参数依据与后续校准

本轮采用通用数字参考档案。数据、端口、有限队列、调度、ECC、复位与
周期事件按该档案验收。厂商宏、工艺、电压、温度角的物理校准状态为
待选型，后续使用宏手册、Liberty 和 STA 结果更新读写延迟、II、保持及
ECC 路径成本。计算引擎与完整 Mesh 的目标侧流量接入继续单独验收。
