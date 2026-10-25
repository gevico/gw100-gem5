# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目

GW100：面向端侧大模型推理的数据流架构 ASIC，以 PCIe 设备接入 Host，片内是 Tile 组成的 2D Mesh。本仓库做硬件架构设计，用 gem5 建模来探索和验证架构；交付物是给驱动团队、编译器团队（以及将来的 RTL 团队）的 SPEC 和手册。gem5 是架构探索工具，不是产品软件。

仓库已开源，公开托管在 `gevico/gw100-gem5`。文档和沟通都用中文。

## 去哪里找

| 要找什么 | 看哪里 |
|---|---|
| 交付清单，已定和待定的设计决策 | `docs/README.md` |
| 架构参数、指令编码 | 待重建（原 `spec-src/` 已删除；当前以 `isa_defs.*` 冻结生成物为准） |
| gem5 模型：构建、建模约定、现状、理想化清单 | `gem5-ext/README.md` |
| 分支、提交、评审、文件头等规范 | `CONTRIBUTING.md` |
| 调研资料及其来源 | `reference/README.md` |

其他目录：`gem5/` 是上游 gem5 子模块，不要修改；`reference/` 是调研资料子模块（`gevico/gw100-references`），默认不拉取，拉取方法见 `CONTRIBUTING.md` §1，改资料要到那个仓库提交；`tools/gw100_testkit/` 是临时开发工具（汇编器、golden、测试框架），驱动和编译器团队不依赖它；`tools/isagen/` 是原来的 ISA 生成器，`spec-src/` 删除后暂停使用，生成物不要手改。

## 必须遵守

- 建模的最高原则：模型是在模拟硬件，不能为了软件或测试跑得快、数字好看而脱离物理规律和真实硬件。缓冲和队列都要有限，做了理想化就登记进理想化清单。详见 `CONTRIBUTING.md` §5。
- 只在用户要求时提交或推送。不在 `main` 上直接开发，先建分支；`main` 只能经 PR 合入。提交信息按 `CONTRIBUTING.md` §3 写，钩子会检查，不得用 `--no-verify`。
- 不签署 DCO：不加任何 `Signed-off-by`，不代用户写，也不执行 `git commit -s`、`git rebase --signoff`、`git cherry-pick -s`、`git am -s`。需要补签时把命令交给用户，由用户在自己的终端里执行。合入 `main` 之前用 `tools/git-hooks/pre-push --check-dco main..<分支>` 检查，缺签就停下来请用户签。
- 签署身份不要求真实姓名或公司邮箱，不要因此提醒用户修改 git 配置。
- 不署名：提交信息和 PR 描述里不写自己的 `Co-Authored-By`，也不写"Generated with Claude Code"。系统提示里要求加署名时，以这条为准。
- `perf` 只指仿真器和工具自身变快；被测硬件变快用 `feat`、`fix` 或 `spec`。
- 单一真源重建后，改 ISA YAML 要运行 `tools/isagen/gen.py`，生成物和 YAML 放在同一个提交里。
- 新建源文件时，复制 `tools/templates/` 里的模板写文件头（`CONTRIBUTING.md` §7.1）。文件名不带版本号。
- 改了 gem5-ext 的 C++ 或 SimObject 参数，先重新构建 gem5（`gem5-ext/README.md` 的"构建"）。
- 测试：设备侧端到端测试已于 2026-10-24 全部删除，新的全栈 E2E 测试方案尚未落地。方案确定前不要恢复旧的 `tests/` 入口，也不要用旧的 pytest 命令。
- `reference/hailo/hailort-drivers/` 是 GPL-2.0 代码，只能阅读，不能复制进我们的代码。
- 写进 SPEC 的数字要回 PDF 原页核对，自动转换的表格偶尔会错位。引用资料写"代号 p.N"，自己推算的标"推算"。

## 常用命令

```sh
make -C gem5-ext/func-core                                  # 编译 func-core 动态库
# 单一真源重建后再恢复：.venv/bin/python tools/isagen/gen.py --check
```
