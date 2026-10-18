# 测试

测试与被测代码分开存放，按模型验证的层次分目录。先确认模型可信，再用它评判设计，验证分为：

- L0：func-core 与 numpy golden 逐位一致；
- L1：功能正确，端到端运行指令；
- L2：部件微基准，与手算的偏差在 ±5% 以内；
- L3：与解析模型、Garnet、DRAMSys 交叉验证；
- L4：外部锚点，复现 M100 的 decode 数据，以及 Tenstorrent 报告里的 GEMV 和 DRAM 带宽现象。

| 目录 | 层次 | 内容 | 需要 gem5 |
|---|---|---|---|
| `l0_func_core/` | L0 | func-core 的 C 接口经 ctypes 与 numpy golden 逐位比对 | 否 |
| `l1_inst/` | L1 | 端到端指令测试：生成测试包、在 gem5 中运行、结果与 golden 比对 | 是 |
| `harness/` | — | 测试框架自身的自检 | 否 |

L2 到 L4 的测试以后按同样方式新建 `l2_*/` 等目录。

在仓库根目录运行：

```sh
uv venv --python 3.12 .venv && uv pip install --python .venv/bin/python -r requirements-dev.txt   # 第一次
make -C gem5-ext/func-core                     # 编出 L0 用的 libgw100fc.so
.venv/bin/python -m pytest                     # 全部测试；没有 gem5 时 L1 自动跳过
make -C gem5-ext/func-core test                # 只跑 L0
.venv/bin/python -m pytest tests/l1_inst/test_vec_ops.py -k bias -s   # 单个用例，-s 显示周期数
.venv/bin/python tools/isagen/gen.py --check   # 检查生成物是否最新
```

导入路径（`tools`，即 `gw100_testkit` 所在目录）和 `gem5` 标记在根目录的 `pytest.ini` 中声明。测试包输出在根目录的 `build/op-tests/<用例名>/`。

L1 用例直接运行 gem5，按 `GEM5_BIN` → 本平台原生构建（macOS 为 `gem5/build/NULL_DARWIN`，Linux 为 `gem5/build/NULL`）的顺序查找，都找不到时自动跳过。构建方法见 [gem5-ext/README.md](../gem5-ext/README.md) 的"构建"一节。

写测试时：

- 多播指令用 `t.local(shape, dtype)` 分配 SRAM，这是本地窗口地址；后门 `load` / `expect` 本地张量时，要用 `tile=` 指明是哪个 Tile；
- 跨 Tile 访问用 `t.sram(tile, ...)`，这是全局地址；
- 测试包里有 `package.json`、`gem5.log`、`result.json` 和 `m5out/stats.txt`，排查问题先看 `gem5.log`。
