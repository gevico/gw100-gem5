"""gem5-ext 的 L1 及以上测试。标记为 gem5 的用例在 gem5 不可用时自动跳过。"""

import sys
from pathlib import Path

import pytest

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "sw"))

from acis import harness  # noqa: E402


def pytest_configure(config):
    config.addinivalue_line("markers", "gem5: 需要编译好的 NULL 版 gem5")


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
