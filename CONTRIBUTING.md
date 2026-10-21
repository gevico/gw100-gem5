# 贡献指南

本指南适用于向本仓库提交改动的所有人，包括架构/gem5 团队、驱动团队、AI 编译器团队以及将来的 RTL 团队；借助 AI 助手生成的改动同样适用。规则参照了以下项目的通行做法：

- [Conventional Commits 1.0](https://www.conventionalcommits.org/zh-hans/v1.0.0/)：提交信息的结构化格式；
- Linux 内核的补丁规范：一个提交只做一件事，正文写清"为什么"；
- gem5 的提交规范：标题带组件标签，每个提交都能编译通过。

本文中的"必须""不得"是硬性要求。其中新增文件的文件头、提交信息格式、分支命名和 DCO 签署由 `tools/git-hooks/` 里的钩子自动检查。

---

## 1. 开始之前

```sh
git clone --recurse-submodules <仓库地址>     # 已经克隆的：git submodule update --init
git config core.hooksPath tools/git-hooks     # 启用文件头、提交信息、分支命名和 DCO 检查（每个克隆做一次）
git config user.name  "<姓名或账号名>"         # 不要求真实姓名；DCO 签署（§3.7）会用到这两项
git config user.email "<邮箱>"
```

调研资料在子模块 `reference/`（约 170 MB），默认不拉取。需要查资料、写 SPEC 时，在本克隆中开启一次，以后 `git submodule update` 会一并更新它：

```sh
git config submodule.reference.update checkout
git submodule update --init reference
```

构建 gem5 见 `gem5-ext/README.md`，运行测试见 `tests/README.md`。

不得用 `--no-verify` 绕过钩子。钩子误判时，请修钩子，并把修复单独提交（scope 用 `git`）。

---

## 2. 分支

### 2.1 分支类型

| 分支 | 用途 | 规则 |
|---|---|---|
| `main` | 唯一的长期分支 | 任何时候都能构建、测试全部通过。不得直接推送，只能经评审后合并；保持线性历史，不产生合并提交 |
| `<类型>/<scope>-<简述>` | 日常开发 | 从最新的 `main` 拉出，生命周期尽量不超过一周，合并后删除 |
| `exp/<简述>` | 设计空间探索 | 可以长期存在，不要求测试通过，但不得直接合入 `main`。结论先写进 SPEC 或 ADR，再把需要保留的模型改动整理成正式分支合入 |
| `release/spec-v<主>.<次>` | 交付版本冻结（对应 `docs/README.md` §3 的 v0.5 / v0.8 / v1.0） | 只接受修正。每个修正也必须合回 `main` |

### 2.2 命名

- 全小写，单词之间用连字符；`<类型>` 与提交类型（§3.3）相同，`<scope>` 取自 `tools/git-hooks/scopes.txt`；
- 有工单号时放在类型后面，例如 `fix/123-ct-dma-backpressure`；
- 例子：`feat/noc-mesh-xy`、`fix/ct-dma-backpressure`、`spec/spec-110-local-window`、`docs/contributing`、`exp/dma-outstanding-sweep`。

### 2.3 同步与改写历史

- 用 `git pull --rebase` 或 `git rebase origin/main` 跟上 `main`。不得把 `main` 合并进工作分支。
- 不得强制推送 `main` 和 `release/*`。
- 自己的工作分支在合并前可以改写历史，强制推送时用 `git push --force-with-lease`。

### 2.4 标签

交付给其他团队的版本必须打附注标签（`git tag -a`），并写明交付内容：

- 文档里程碑：`spec-v0.5`；
- 单一真源升版本时：`isa-v0.2`、`arch-params-v0.2`，与 YAML 里的 `version` 一致。

---

## 3. 提交

### 3.1 原则

- **一个提交只做一件逻辑上完整的事。** 格式调整、重命名、移动文件，要与逻辑修改分开提交。
- **每个提交都必须能构建，测试全部通过。** 这样才能用 `git bisect` 定位问题。
- **生成物和源文件放在同一个提交里。** 例如改了 `spec-src/isa/*.yaml`，就同时提交重新生成的 `isa_defs.*`。不得手改生成物。
- **不得提交：** 构建产物、虚拟环境、测试包输出、个人编辑器配置、密钥。`.gitignore` 已覆盖常见情况。超过 5 MB 的文件先在评审中说明理由。
- **不得修改 `gem5/` 子模块的内容。** 确实需要改 gem5 本体时，做成补丁放进 `gem5-ext/patches/`，并注明原因。升级 gem5 时，把子模块指针的变更单独提交，scope 用 `gem5`。

### 3.2 提交信息格式

```
<类型>(<scope>[,<scope>])[!]: <标题>

<正文>

<脚注>
```

| 部分 | 要求 |
|---|---|
| 类型 | 必填，见 §3.3 |
| scope | **必填**，取自 [`tools/git-hooks/scopes.txt`](tools/git-hooks/scopes.txt)，最多两个，用英文逗号分隔。涉及更多组件时，应当拆分提交。新增组件时，先在该文件登记 scope |
| `!` | 破坏兼容性时加，见 §3.4 |
| 标题 | 用中文写清做了什么，不写"修改了某文件"。末尾不加标点。整行不超过 72 个显示列（一个汉字算 2 列） |
| 正文 | 与标题之间空一行。每行不超过 72 列；代码和日志缩进 4 格，链接不受此限。内容见下 |
| 脚注 | git trailer 格式（`Key: value`），与正文之间空一行，见 §3.5 |

正文回答三个问题：

1. **为什么改**：要解决的问题或动机。
2. **怎么改的**：改动要点，不要逐行复述 diff。
3. **影响**：对其他团队（驱动、编译器）的影响、对模型精度的影响，以及测试结果。

正文里写数字结论时，注明出处：引用资料写"代号 p.N"，自己推算的写"推算"，跨资料对比得出的写"观察"。

### 3.3 类型

| 类型 | 用途 |
|---|---|
| `feat` | 新功能：新的硬件模型、新指令、新工具能力 |
| `fix` | 修正错误，包括模型与真实硬件不符的错误 |
| `spec` | SPEC 文档或单一真源（`arch_params.yaml`、`isa/*.yaml`）的内容变更 |
| `perf` | **只指**仿真器和工具自身运行得更快。被测硬件的性能变化不属于 `perf`，要用 `feat`、`fix` 或 `spec`，并遵守 §5 |
| `refactor` | 不改变行为的代码重构 |
| `style` | 只改格式，例如 clang-format、Black |
| `test` | 只增改测试 |
| `docs` | 手册、README、注释等文档 |
| `build` | 构建系统、依赖、gem5 子模块 |
| `ci` | 持续集成配置 |
| `chore` | 其他杂项 |
| `revert` | 撤销之前的提交。正文写明被撤销提交的哈希和撤销原因 |
| `exp` | 实验性改动，只允许出现在 `exp/*` 分支上 |

### 3.4 破坏性改动

以下改动属于破坏兼容性，标题必须带 `!`，并加 `BREAKING CHANGE:` 脚注说明迁移方法：

- ISA 的编码或语义；
- `arch_params.yaml` 里参数的名字或语义；
- 地址映射；
- Host 接口的命令记录、完成记录和管理 Tile 寄存器；
- 测试包格式。

同时必须升高对应 YAML 的 `version`。评审要求见 §4.3。

### 3.5 脚注

| 脚注 | 何时必须写 |
|---|---|
| `BREAKING CHANGE: <迁移说明>` | 标题带 `!` 时 |
| `Idealization: add #N` / `remove #N` / `update #N` | 新增、消除或修改了 `gem5-ext/README.md` "理想化清单"里的条目时。多项用 `, ` 分隔 |
| `Refs: #123` / `Closes: #123` | 关联工单时 |
| `Co-Authored-By: 姓名 <邮箱>` | 有其他人类参与时。它只说明谁参与了写作，不等于 DCO 签署。**不接受 AI 署名为共同作者**，见 §3.8 |
| `Signed-off-by: 姓名 <邮箱>` | 进入 `main` 和 `release/*` 的每个提交都必须有，**只能由人类签署**，见 §3.7。放在脚注的最后 |

### 3.6 示例

```
feat(ct-ctrl,mgmt): SRAM 操作数改为 Tile 相对寻址

同一条指令要能多播给多个 Tile（SPMD），SRAM 操作数就不能写死某个
Tile 的全局地址。指令中落在本地窗口 local_sram_base 内的地址，指执行
该指令的 Tile 的 SRAM。

- Tile 控制器在指令到达时换算地址，功能单元只看到全局地址
- 派发器下发前校验每个目标 Tile，越界时返回出错状态

测试：L0 97/97，L1 11/11。16 Tile SPMD 用例的派发条目由 80 降到 50。

Idealization: add #5
```

### 3.7 DCO 签署

`Signed-off-by` 表示签署者按 [Developer Certificate of Origin 1.1](https://developercertificate.org/)（DCO）作出证明：自己有权以本仓库的方式提交这份改动，并对它负责。这是有法律意义的声明，**只能由人类签署**。

- **进入 `main` 和 `release/*` 的每个提交都必须至少有一个人类的 `Signed-off-by`**，姓名和邮箱与签署者的 git 身份一致。工作分支上的提交可以暂时不签，评审通过后、合并之前补签。
- **签署身份不要求真实姓名，也不要求公司邮箱**：可以用账号名、个人邮箱等在团队内能对应到具体个人的固定身份。不得匿名，也不得用 AI 或机器人的名字。
- **AI 不得签署 DCO。** AI 助手不得以任何名义添加 `Signed-off-by`：不能写自己的名字，也不能代人类写。AI 也不得执行会添加签署的命令，包括 `git commit -s` / `--signoff`、`git rebase --signoff`、`git cherry-pick -s`、`git am -s`。
- **AI 参与的提交由人类审阅后签署。** AI 不署名（§3.8），也不签署；人类读过改动、愿意为它负责之后，自己添加 `Signed-off-by`。签署者对改动负全部责任，包括 AI 写的部分。
- **补签的方法**：由人类在自己的终端里，在工作分支上执行：

      git rebase --signoff main                  # 为分支上相对 main 的每个提交加上自己的签署
      git commit --amend --signoff --no-edit     # 只签最新的一个提交

脚注的顺序是先写其他脚注，最后写 `Signed-off-by`：

```
Idealization: add #9
Co-Authored-By: 李四 <lisi@example.com>
Signed-off-by: 张三 <zhangsan@example.com>
```

commit-msg 和 pre-push 钩子会拦住明显的违规（识别规则见 `tools/git-hooks/dco.py`），本地合并前可以用 `tools/git-hooks/pre-push --check-dco main..<分支>` 检查。AI 冒用人类姓名签署，钩子识别不了，靠本规则和评审约束。

### 3.8 不接受 AI 署名

本仓库不接受 AI 署名为作者或共同作者。提交信息和合并请求描述里，不得出现 AI 的 `Co-Authored-By`，也不得出现"Generated with Claude Code"之类的署名行。改动由提交它、签署它的人类负责。

- 本仓库的 `.claude/settings.json` 已关闭 Claude Code 的自动署名，个人配置 `.claude/settings.local.json` 不得重新打开。用其他 AI 助手时，也要关闭它的自动署名。
- commit-msg 钩子会拒绝 AI 的 `Co-Authored-By` 和"Generated with Claude Code"署名行。

---

## 4. 合并请求与评审

### 4.1 合并请求的内容

- 标题与提交标题的格式相同。
- 描述包含：
  1. 背景：为什么改；
  2. 改动：主要内容；
  3. 测试：实际运行的命令和结果；
  4. 对其他团队的影响；
  5. 建模原则自查：§5 的清单逐项勾选。
- 规模尽量不超过 400 行有效改动（不含生成物和测试数据）。更大的改动拆成多个合并请求。

### 4.2 合并前

- rebase 到最新的 `main`。
- 整理提交历史：回应评审意见时用 `git commit --fixup=<提交>`，合并前用 `git rebase -i --autosquash` 压缩。
- §6 列出的检查全部通过。
- 每个提交都有人类的 `Signed-off-by`（§3.7）。缺少时，由审阅过改动的人类执行 `git rebase --signoff main` 补签，再用 `tools/git-hooks/pre-push --check-dco main..<分支>` 确认。
- 合并方式默认用 rebase 合并，保留每个有意义的提交。如果分支里都是零碎提交，可以 squash 合并，但合并后的提交信息必须按 §3 重写。

### 4.3 评审人

合并请求至少要有一名评审人批准。仓库 admin 合并时可以不等批准（GitHub 规则集为 admin 设置了经 PR 的豁免），但下列改动仍应先请相应团队评审：

| 改动 | 应参与评审 |
|---|---|
| `spec-src/isa/`、Host 接口记录、管理 Tile 寄存器 | 驱动团队、AI 编译器团队 |
| `arch_params.yaml` 中软件可见的参数（地址映射、SRAM 容量、Tile 数等） | AI 编译器团队 |
| 破坏性改动（§3.4） | 所有受影响团队 |
| `docs/spec/` 中的某份 SPEC | 该 SPEC 的主要读者团队 |

---

## 5. 建模原则（gem5-ext 改动必须自查）

**gem5 模型是在模拟硬件。** 建模取舍必须符合物理规律和真实硬件的实现方式。绝不能为了让上层软件跑得更快、让测试跑得更快，或者让数字更好看，就让模型脱离实际。

改动 `gem5-ext/` 时，合并请求里要逐项确认：

- [ ] 新增的缓冲、队列、端口和在途请求数都有上限，容量取自 `arch_params.yaml`；
- [ ] 数据搬运和控制动作都花了时间。暂时做了理想化的，已经登记进 `gem5-ext/README.md` 的"理想化清单"，注明偏乐观还是偏悲观，并在提交里写了 `Idealization:` 脚注；
- [ ] 如果改动让被测硬件的性能变好了，已写明对应的硬件代价（面积、功耗、复杂度）和依据（参考设计或推算）；
- [ ] 测试基础设施（TestHost、后门加载、harness）的改动没有改变被测硬件的结构、容量和时序；
- [ ] 从参考设计派生的参数，只缩放了确实随时钟变化的量，以纳秒计的物理延迟没有跟着缩。

---

## 6. 提交前的检查

合并前，生成物要是最新的，全部测试要通过，命令见 `tests/README.md`。改了 `gem5-ext/src/` 或 SimObject 的 Python 参数，先重新构建 gem5，见 `gem5-ext/README.md`。

测试要求：

- 测试统一放在仓库根目录的 `tests/`，按验证层次分目录（`l0_func_core/`、`l1_inst/` 等），不放进被测代码的目录。
- 新指令要同时有 L0 用例（func-core 与 golden 逐位比对）和 L1 用例（端到端）。
- 修 bug 要先写一个能复现问题的测试。
- 改了时序模型，要附上 L2 微基准或手算对比的结果，偏差在 ±5% 以内。暂时做不到的，在合并请求里说明原因。

---

## 7. 代码与文档风格

- **C++ 和 Python** 遵循 gem5 的规范（`gem5/AGENTS.md`）：4 空格缩进，每行不超过 79 列，用 clang-format / Black 格式化。注释用中文，标识符用英文，注释密度与周围代码保持一致。
  - 现状：现有代码中有不少行超过 79 列，将用一个单独的 `style` 提交统一整改。整改之前，新增和修改的行就要符合要求。
- **文档**：
  - 用 Markdown 写；框图用 Mermaid，时序图用 WaveDrom，包格式和位域用表格。
  - 交付文档命名为 `<编号>-<英文短名>.md`，例如 `docs/spec/SPEC-210-matrix-engine.md`。
  - "必须""应""可"分别对应 MUST、SHOULD、MAY，只在 SPEC 里使用；还没定的内容标 `TBD`。
  - 参数只在单一真源中维护，文档里引用参数名，不写死数字。
  - 引用调研资料写"代号 p.N"；写进 SPEC 的数字要回 PDF 原页核对；自己推算的标"推算"，跨资料对比得出的判断标"观察"。
- **文件名不带版本号**：不要写 `isa-v0.yaml`、`spec-v2.md` 这样的名字。历史版本由 git 记录；需要标明版本时，写在文件内的 `version` 字段里，交付时再打 git 标签（§2.4）。

### 7.1 文件头注释

每个**新增**的源文件都必须带 Doxygen 风格的文件头注释，让人和 agent 打开文件就知道它是什么、对应哪份规格。

**适用范围**：`gem5-ext/`、`tools/`、`tests/` 下的 C++ 文件（`.hh`、`.h`、`.cc`、`.cpp`、`.hpp`）和 Python 文件（`.py`）。下列文件不要求：
- 生成文件，前几行注明了"请勿手改"；
- `SConscript`、`Makefile` 等构建脚本；
- `reference/`、`gem5/` 下的第三方内容。

新建文件时复制 [`tools/templates/`](tools/templates/) 里的模板，替换掉全部占位符。C++ 的文件头是文件开头的第一个 `/** ... */` 块，Python 的是模块 docstring，字段相同：

```cpp
/**
 * @file  dma_engine.hh
 * @brief Compute Tile 的 DMA 引擎
 *
 * 本地口直连本 Tile SRAM，远端口接 NoC，边读边写。
 *
 * @see SPEC-250, SPEC-320
 */
```

| 字段 | 要求 |
|---|---|
| `@file` | 必填。只写文件名，不带路径，必须与实际文件名一致；改名时同步修改 |
| `@brief` | 必填。一行中文，说明这个文件是什么，末尾不加句号。`.cc` 文件可以写"XXX 的实现" |
| 详细说明 | 可选。与 `@brief` 之间空一行，写职责、关键设计取舍、与其他组件的关系、已知简化；不复述代码 |
| `@see` | 有对应的 SPEC 或 MAN 时必填，写编号，多个用逗号分隔。纯工具或测试文件没有对应规格时可以省略 |

**不写**：作者、日期、修改记录，因为 git 已经记录了这些，写在文件头里很快会过时；也不写版权和许可证声明。

pre-commit 钩子只检查新增的文件；改名时要同步修改 `@file`，这一点由评审把关。

---

## 8. 第三方代码与资料

- **gem5**：以子模块方式引入，固定在 `.gitmodules` 和子模块指针记录的 commit。规则见 §3.1。
- **调研资料（`reference/`）**：放在独立仓库 [`gevico/gw100-references`](https://github.com/gevico/gw100-references)，以子模块挂在 `reference/`（2026-10-21 拆出）。本项目是开源的，调研资料随仓库公开托管、对外分发（2026-10-18 确认）。资料的版权仍归原作者或出版方所有，引用时注明出处；新增资料只收官方或开放渠道发布的版本。
  - 增改资料先在 `gw100-references` 合入，再在本仓库单独提交子模块指针的更新，scope 用 `reference`。
  - PDF 原件和转换出的图片直接入库，不用 Git LFS（2026-10-18 决定）。资料会永久留在 git 历史里，所以只收确有参考价值的资料，单个文件超过 5 MB 时，在合并请求里说明理由。
  - Hailo 的文档每页标有 Confidential。这些文档取自 Hailo 官网公开下载的版本，维护者已确认可以随仓库公开（2026-10-18）。
  - `reference/hailo/hailort-drivers/` 是 GPL-2.0 代码，只能阅读，不得把其中的代码复制进我们的代码。
  - 新增资料的流程见 `reference/README.md` 的"新增资料的方式"。
