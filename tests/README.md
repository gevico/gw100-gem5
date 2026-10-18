# 测试

测试与被测代码分开存放，按模型验证的层次分目录（层次定义见 `CLAUDE.md`"gem5 建模约定"）。

| 目录 | 层次 | 内容 | 需要 gem5 |
|---|---|---|---|
| `l0_func_core/` | L0 | func-core 的 C 接口经 ctypes 与 numpy golden 逐位比对 | 否 |
| `l1_inst/` | L1 | 端到端指令测试：生成测试包、在 gem5 中运行、结果与 golden 比对 | 是 |
| `harness/` | — | 测试框架自身的自检 | 否 |

以后的 L2（部件微基准）、L3（交叉验证）、L4（外部锚点）按同样方式新建 `l2_*/` 等目录。

在仓库根目录运行：

```sh
make -C gem5-ext/func-core            # 先编出 L0 用的 libgw100fc.so
.venv/bin/python -m pytest            # 全部测试；没有 gem5 时 L1 自动跳过
```

导入路径（`tools`，即 `gw100_testkit` 所在目录）和 `gem5` 标记在根目录的 `pytest.ini` 中声明。测试包输出在根目录的 `build/op-tests/<用例名>/`。

L1 用例默认直接运行本平台原生构建的 gem5，每个用例只启动一个 gem5 进程；没有原生构建时才在 Docker 里运行。构建方法和选择顺序见 `CLAUDE.md` 的"常用命令"。
