"""全部测试的公共配置：标记为 gem5 的用例在 gem5 不可用时自动跳过。

导入路径（gem5-ext/sw）和 gem5 标记在仓库根目录的 pytest.ini 中声明。
"""

import pytest
from acis import harness


def pytest_collection_modifyitems(config, items):
    if not any("gem5" in item.keywords for item in items):
        return
    ok, why = harness.gem5_available()
    if ok:
        return
    skip = pytest.mark.skip(reason=f"gem5 不可用：{why}")
    for item in items:
        if "gem5" in item.keywords:
            item.add_marker(skip)
