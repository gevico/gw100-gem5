# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目与当前阶段

- 产品：面向端侧大模型推理的数据流架构 ASIC。独立芯片，以 PCIe 设备形式接入 host，片内是多个 Tile 组成的 2D Mesh。
- 团队：架构/gem5 团队（本仓库）、驱动团队、AI 编译器团队；将来还会有 RTL 团队。本仓库的交付物是给后几个团队的 SPEC 和手册。
- **当前阶段：硬件架构设计**，包括 ISA、DMA、地址空间、NoC、Tile 划分与内部结构、Host 接口。
- **gem5 的定位是架构探索和验证工具**：用来判断架构是否合理，并通过设计空间探索持续改进。它不是产品软件，也不是 RTL 的替身。
- 文档和沟通都用中文。

## 仓库结构

- `docs/README.md`：**交付清单的权威来源**。内容包括：DOC / SPEC / MAN 编号与读者、版本规则（v0.5 可建模、v0.8 可实现、v1.0 冻结）、编写约定（§7）、**已确定的决策与待决事项（§8）**。`docs/` 下的 `general/`、`spec/`、`manual/`、`adr/` 目前还是空目录。
- `reference/`：调研资料库。
  - `README.md`：资料索引和每份资料的来源。
  - `SURVEY.md`：跨厂商对比、KPI 推算、对各份 SPEC 的启示。
  - `LESSONS.md`：各家的设计精髓，以及提炼出的设计原则 P1–P16。
  - `<厂商>/NOTES.md`：关键参数及出处页码。
  - 每份 PDF 旁边有自动转换的同名 `.md` 和 `images/`。
- `tools/ref2md/`：PDF 转 Markdown 的工具（docling）。
- `gem5/`：上游 gem5 v25.1.0.1，以 git 子模块引入，固定在 commit `f5c5a6e`（stable 分支，浅克隆）。**不要修改它**。上游的开发规范见 `gem5/AGENTS.md`。
- `spec-src/`：单一真源。`arch_params.yaml` 是架构参数，`isa/isa-v0.yaml` 是指令编码、Host 接口记录和管理 Tile 寄存器。
- `tools/isagen/gen.py`：由 `isa-v0.yaml` 生成 `gem5-ext/func-core/include/acis/isa_defs.hh` 和 `gem5-ext/sw/acis/isa_defs.py`。**这两个文件不要手改**，改 YAML 后重新生成。
- `gem5-ext/`：我们的 gem5 扩展，通过 `EXTRAS` 编译进 gem5。
  - `func-core/`：不依赖 gem5 的功能核心（数值规则、指令编解码、向量运算）。gem5 的 SConscript 直接编译它的源码，同时也单独编成 `libacisfc.so`，供 L0 测试用。
  - `src/`：SimObject，按硬件组件分目录。`compute_tile/` 下有 sram、sync、vector、dma、controller；另有 `mgmt_tile/`（派发器）、`testhost/`、`common/`（`MemStreamer`：把字节段拆成请求并跟踪完成前缀）。
  - `configs/`：`chip.py` 按 arch_params 搭建整芯片，`run_op.py` 运行一个测试包，`lpddr5x.py` 是由 LPDDR5-6400 派生的近似时序。
  - `sw/acis/`：汇编器 `asm.py`、numpy golden `golden.py`、地址映射 `addrmap.py`，以及测试框架 `harness.py`（生成测试包 → 调 gem5 → 比对 golden）。
- `tests/`：全部测试，与被测代码分开，按验证层次分目录：`l0_func_core/`（func-core 与 golden 逐位比对）、`l1_inst/`（端到端指令测试）、`harness/`（测试框架自检）；以后的 L2~L4 也放在这里。标记为 `gem5` 的用例在没有 gem5 时自动跳过。导入路径和标记在根目录的 `pytest.ini` 里声明。
- `.venv/`：仓库级 Python 3.12 环境（numpy、pyyaml、pytest，见 `requirements-dev.txt`），用于 L0/L1 测试和代码生成。
- 仓库根目录是 git 仓库，默认分支为 `main`。贡献规范见 `CONTRIBUTING.md`，钩子在 `tools/git-hooks/`，scope 清单在 `tools/git-hooks/scopes.txt`。

## 常用命令

gem5 统一在 Docker 镜像里构建和运行（见 `Dockerfile` 头部注释）：

```sh
# Docker Desktop 默认只分配 8 GiB 内存，-j$(nproc) 容易 OOM，要显式限制并行度
docker build -t gem5-dev --build-arg BUILD_JOBS=6 .
docker build -t gem5-slim --target base .        # 只要工具链，不预编译

# 挂载宿主机源码做增量开发
docker run --rm -it -v "$PWD/gem5:/gem5" gem5-dev
scons build/X86/gem5.opt -j6                       # 在容器内执行

# 我们的模型：不带 CPU 的 NULL 构建，外挂 gem5-ext。
# 仓库根目录按原路径挂载进容器，这样 harness 写出的测试包路径在容器内外一致。
# gem5-dev 与 gem5-slim 的工具链相同，哪个在本机就用哪个。
docker run --rm -v "$PWD:$PWD" -v acis-ccache:/home/gem5/.ccache -w "$PWD/gem5" gem5-dev \
    scons EXTRAS="$PWD/gem5-ext" build/NULL/gem5.opt -j6
```

- 第一次构建是全量的，要几十分钟：镜像里预编译的是 `/gem5/build/X86`，用不上。之后 `gem5/build/NULL` 留在宿主机上，ccache 也保存在具名卷里；只改 gem5-ext 时，增量构建约 1–2 分钟。
- 改了 SimObject 的 Python 参数之后，也要重新构建。

我们自己的测试和代码生成（在仓库根目录执行）：

```sh
uv venv --python 3.12 .venv && uv pip install --python .venv/bin/python -r requirements-dev.txt
.venv/bin/python tools/isagen/gen.py            # 由 isa-v0.yaml 重新生成 isa_defs
.venv/bin/python tools/isagen/gen.py --check    # 只检查生成物是否最新
make -C gem5-ext/func-core                      # 编出 L0 测试用的 libacisfc.so
.venv/bin/python -m pytest                      # 全部测试：L0 + harness 自检 + L1（L1 自动在容器里跑 gem5）
make -C gem5-ext/func-core test                 # 只跑 L0
.venv/bin/python -m pytest tests/l1_inst/test_vec_ops.py -k bias -s   # 单个用例
```

- 本机已有可执行的 gem5 时，设 `GEM5_BIN=<路径>` 绕过 Docker；用 `ACIS_GEM5_IMAGE` 指定镜像，默认取本机已有的 `gem5-slim` 或 `gem5-dev`。
- 在 `OpTest` 里写测试时：
  - 多播指令用 `t.local(shape, dtype)` 分配 SRAM，这是本地窗口地址；
  - 后门 `load` / `expect` 本地张量时，要用 `tile=` 指明是哪个 Tile；
  - 跨 Tile 访问用 `t.sram(tile, ...)`，这是全局地址。
- 每个测试包写在仓库根目录的 `build/op-tests/<用例名>/`，里面有 `package.json`、`gem5.log`、`result.json`、`m5out/stats.txt`。
- 调试时给 `OpTest.run(debug_flags=[...])` 传 `AcisDispatch`、`AcisCtrl`、`AcisUnit`。
- Docker 构建 gem5 很占磁盘。宿主机磁盘曾经满到 98%，导致 Docker Desktop 的镜像存储报 `input/output error`。构建前至少留出约 40 GB 空闲空间。

gem5 自带的测试（详见 `gem5/AGENTS.md`）：

```sh
scons build/ALL/unittests.opt                      # C++ GoogleTest 单元测试
cd tests && ./main.py run -j <jobs>                # 快速 TestLib 测试集
cd tests && ./main.py run --uid <SuiteUID> --skip-build   # 只跑单个测试集
```

参考资料的 PDF 转换（在仓库根目录执行；虚拟环境约 1.1 GB，已加入 `.gitignore`）：

```sh
uv venv --python 3.12 tools/ref2md/.venv
uv pip install --python tools/ref2md/.venv/bin/python -r tools/ref2md/requirements.txt
tools/ref2md/.venv/bin/python tools/ref2md/convert.py              # 只转换新增或更新过的 PDF
tools/ref2md/.venv/bin/python tools/ref2md/convert.py --force a.pdf  # 强制重新转换指定文件
```

转换规则：
- 每页以 `<!-- p.N -->` 开头，引用页码时以此为准。
- 竖版文档（论文、数据手册）只导出图片区域。
- 横版文档（讲稿）每页导出一张整页图，因为框图常由矢量文本框拼成，识别不出图片区域。

## Git 与提交（必须遵守 `CONTRIBUTING.md`）

- 只在用户要求时提交或推送。不在 `main` 上直接开发：先建 `<类型>/<scope>-<简述>` 分支。唯一的例外是空仓库的第一个提交。
- 提交信息：
  - 标题格式为 `<类型>(<scope>)[!]: <中文标题>`，scope 必填，取自 `tools/git-hooks/scopes.txt`，最多两个；标题末尾不加标点，整行不超过 72 显示列（汉字算 2 列）。
  - 正文写为什么改、怎么改、影响和测试结果，每行不超过 72 列。
- 脚注：
  - 标题带 `!` 时必须写 `BREAKING CHANGE:`；
  - 增删理想化清单条目时写 `Idealization: add/remove/update #N`；
  - Claude 生成的提交写 `Co-Authored-By`。
- `perf` 只指仿真器和工具自身更快。被测硬件的性能变化用 `feat`/`fix`/`spec`，并遵守建模原则。
- 每个提交只做一件事，必须能构建、测试通过；改 YAML 就同一提交带上重新生成的 `isa_defs.*`。
- 不得用 `--no-verify` 绕过钩子。新克隆要先执行 `git config core.hooksPath tools/git-hooks`。

## gem5 建模约定（已确定的方向）

- **最高原则：gem5 模型是在模拟硬件。** 建模取舍必须符合物理规律和真实硬件的实现方式。绝不能为了让上层软件跑得更快、让测试跑得更快，或者让数字更好看，就让模型脱离实际。具体要求：
  - 缓冲、队列、端口、在途请求数都必须有限，容量取自 `arch_params.yaml`；
  - 数据搬运和控制动作都要花时间。暂时做了理想化的（例如零时间、无限缓冲），必须登记在 `gem5-ext/README.md` 的"理想化清单"里，注明结果因此偏乐观还是偏悲观，以及计划何时消除；
  - 通过调参数改善性能时，要同时计入对应的硬件代价（面积、功耗、复杂度），并给出依据（参考设计或推算）；
  - 测试基础设施（TestHost、后门加载、harness）只能改变测试怎么组织，不能改变被测硬件的结构、容量和时序。例如不能为了省内存缩小 DRAM 容量或器件规格；
  - 从参考设计派生参数时，只缩放确实随时钟变化的量，以纳秒计的物理延迟不能跟着缩。
- **采用外挂方式**：我们的代码全部放在 `gem5-ext/`，通过 `EXTRAS` 编译进同一个 gem5 可执行文件。确实绕不开的本体改动，做成补丁放进 `gem5-ext/patches/`，基于上面的固定 commit，每个补丁注明原因。
- **按张量指令粒度建模**，不逐个 MAC 仿真。
- **ISA 不放进 `gem5/src/arch`**：那套框架是给 CPU 指令集用的。张量指令由自定义的 Tile SimObject 解码；编码以 YAML 为唯一来源，同时生成 gem5 的译码器和 Python 汇编器。
- **功能计算和时序分开**：功能计算由一个不依赖 gem5 的功能核心库完成，同时作为 golden 参考；跑大形状扫参数时可以只跑时序。
- **NoC**：v0 用自研的轻量 Mesh 模型，粒度是 DMA 传输，支持多播。Garnet 只用来交叉验证。
- **DRAM**：gem5 只有 `LPDDR5_5500 / 6400_1x16` 两种模型，LPDDR5X-8533 要在我们自己的配置里派生。目标是 8 个 x16 通道，即 128-bit。
- **没有驱动和 host 时的算子验证**：用一个 `TestHost` SimObject 代替 host，流程如下：
  1. 后门加载：仿真开始前用功能访问直接写入张量，不占仿真时间；
  2. 前门启动：按 SPEC-400 的格式写命令队列、敲 doorbell，走正常的时序路径；
  3. 测量窗口：从敲 doorbell 到收到完成通知；
  4. 后门读回：读出输出并写文件；
  5. Python 侧与 golden 对比：整数逐位一致，浮点在误差界内。
- **模型验证分四层**，先确认模型可信，再用它评判设计：
  - L1 功能正确；
  - L2 部件微基准，与手算偏差在 ±5% 以内；
  - L3 交叉验证：对比解析模型、Garnet、DRAMSys；
  - L4 外部锚点：复现 M100 的 decode 数据，以及 Tenstorrent 报告里的 GEMV 和 DRAM 带宽现象。
- **代码风格**：`gem5-ext/` 里的 C++ 和 Python 遵循 gem5 的规范（见 `gem5/AGENTS.md`：4 空格缩进，每行 79 列，clang-format / Black）。
- **新建源文件时**，先复制 `tools/templates/` 里的模板，按 `CONTRIBUTING.md` §7.1 写文件头（Doxygen 风格的 `@file`、`@brief`、`@see`；不写作者、日期、版权）。pre-commit 钩子会检查。

## 架构基线

- 以 `docs/README.md` §8 为准，数值以 `spec-src/arch_params.yaml` 为准。
- 2026-10-18 用户确认了 Device 侧架构图 v0，以下四项随之确定：
  1. 集中派发张量级指令（M100 式），每个功能单元顺序执行，用计数器同步；
  2. 6×4 Mesh，含 4×4 个 Compute Tile，每个 Tile 有 8×64×4 点积阵列、2 MB SRAM，主频 1 GHz；
  3. 自研轻量 NoC 模型，Garnet 只做交叉验证；
  4. 基准模型以 LLaMA2-7B 为主、Llama-3.2-1B 为辅。
- 同日确定：**SRAM 操作数用 Tile 相对寻址**。
  - 指令里落在 `address_map.local_sram_base` 本地窗口内的地址，指执行该指令的 Tile 的 SRAM，所以一条指令可以多播给多个 Tile（SPMD）。
  - 换算由 Tile 控制器在指令到达时完成；功能单元只看到全局地址。换算逻辑在 func-core 的 `resolveLocal`。
  - 越出 SRAM 的程序由派发器在派发前拒绝，返回出错状态。
- 仍按默认值推进的：PCIe Gen4 x4，约 15–25 W；权重 INT4；LPDDR5X-8533，128-bit（约 136 GB/s）。
- 核心判断：权重常驻 DRAM，**decode 受 DRAM 带宽限制**。具体推算见 `reference/SURVEY.md` §4，原则见 `reference/LESSONS.md` P1。

## gem5 v0 模型现状

- **已编译通过，L1 全部通过**：11 个 gem5 端到端用例，结果与 golden 逐位一致，单个用例约 1 秒。
- 已实现的指令：NOP、END、SYNC、DMA_COPY、VEC_BINARY、VEC_SCALAR。MATMUL、VEC_UNARY、VEC_REDUCE 只定义了编码；派发器遇到未实现的操作码会拒绝执行，返回出错状态。
- 片上网络暂时用 `NoncoherentXBar` 代替，**还不能反映 Mesh 拓扑和多跳延迟**。自研 Mesh 模型是下一步工作。
- 跨 Tile 的 `sig_tile` 通知尚未实现，用到时会 panic。
- PCIe 不建时序：Host 内存只是一块 `SimpleMemory`。
- 与真实硬件不一致的简化逐条列在 `gem5-ext/README.md` 的"理想化清单"里；标为"高"的要在 L2 校准前消除。
- **时序还没有经过 L2 校准**，目前的周期数只能说明流程跑通了。初步观察：单 Tile 的 DMA 读 DRAM 只有约 16 B/周期，怀疑在途请求数（32 × 64 B）不足以覆盖 DRAM 往返延迟。
- 配置层次：每个 Compute Tile 是一个 `SubSystem`，路径为 `system.ct[i].{sram,sync,ve,dma,ctrl}`，统计名形如 `system.ct0.ve.unit.busyTicks`。gem5 会把作为参数传入的孤儿 SimObject 收养为子对象，所以要先把子对象挂到容器上，再互相引用。
- 编码坑：
  - func-core 在顶层命名空间 `::acis`，gem5 侧代码在 `gem5::acis`；gem5 侧只包含 `src/common/fc.hh`，由它把需要的名字引入。
  - DRAM 按 256 B 交织，功能访问不能跨通道，所以 TestHost 的后门访问按 64 B 对齐切分。

## 文档与资料约定

- **单一真源**：架构参数、寄存器、指令编码只维护机器可读的源文件，分别是 `spec-src/arch_params.yaml`、`spec-src/isa/*.yaml`（寄存器暂时也放在这里），将来还有 `regs/*.rdl`。文档里引用参数名，不手写数字。
- **引用资料**：写作"代号 p.N"。数字写进 SPEC 之前，必须回 PDF 原页核对，因为自动转换的表格偶尔会错位。自己推算的结论标"推算"，跨资料对比得出的判断标"观察"。
- **新增资料**：
  - 每份资料单独一个目录 `<厂商>/<年份>-<会议>-<短名>/`；
  - 优先从官方或开放渠道下载，并在 `reference/README.md` 登记来源；
  - GitHub 上的文档按固定 commit 下载，commit 号写进 `SOURCE_COMMIT`，不带 `.git` 目录；
  - 下载后运行 `convert.py`。
- **网络限制**：`dl.acm.org`、`cacm.acm.org`、`docs.amd.com` 在当前网络下 TLS 握手被重置；`hailo.ai` 的文件会返回 403。可以改取官方链接在 Wayback 上的存档：`https://web.archive.org/web/<年份>id_/<原始链接>`。
- **版权**：资料仅供内部参考。Hailo 的文档标有 Confidential，不能外传。`reference/hailo/hailort-drivers/` 是 GPL-2.0 代码，只能阅读，不能复制进我们的代码。
