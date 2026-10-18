"""
@file  test_harness.py
@brief 测试框架自检：测试包格式和结果比对

不需要 gem5。

@see MAN-060
"""

import json

import numpy as np
import pytest
from gw100_testkit import addrmap, asm
from gw100_testkit.harness import CMD_OFFSET, CPL_OFFSET, PROG_OFFSET, OpTest, RunResult
from gw100_testkit.isa_defs import COMMAND_RECORD


def _small_test():
    t = OpTest("selftest")
    a = t.dram((8,), "FP16")
    c = t.sram(0, (8,), "FP16")
    t.load(a, np.arange(8, dtype=np.float16))
    t.add(0, asm.dma_copy(a, c))
    t.add(0, asm.end())
    t.expect("c", c, np.arange(8, dtype=np.float16))
    return t, c


def _fake_gem5(pkg, status=2, out=None):
    """按 TestHost 的输出格式伪造一次运行结果。"""
    spec = json.loads((pkg / "package.json").read_text())
    for d in spec["dumps"]:
        if d["file"].endswith("completion.bin"):
            raw = bytearray(32)
            raw[0:4] = status.to_bytes(4, "little")
            raw[8:16] = (1).to_bytes(8, "little")
            (pkg / d["file"]).write_bytes(bytes(raw))
        else:
            (pkg / d["file"]).write_bytes(out)
    (pkg / "result.json").write_text(json.dumps({
        "status": status, "tag": 1, "start_tick": 0, "end_tick": 5000,
        "device_ticks": 5000, "sim_tick": 6000, "tick_freq": 10 ** 12}))


def test_package_layout(tmp_path):
    t, c = _small_test()
    pkg = t.write_package(tmp_path / "pkg")
    spec = json.loads((pkg / "package.json").read_text())
    host = addrmap.host_mem_base(t.p)
    assert spec["cmd_record_addr"] == host + CMD_OFFSET
    assert spec["completion_addr"] == host + CPL_OFFSET

    loads = {l["addr"]: (pkg / l["file"]).read_bytes() for l in spec["loads"]}
    cmd = loads[host + CMD_OFFSET]
    off, _ = COMMAND_RECORD["program_addr"]
    assert int.from_bytes(cmd[off:off + 8], "little") == host + PROG_OFFSET
    prog = loads[host + PROG_OFFSET]
    assert prog == t.program.encode()
    assert prog[:4] == b"GW10"          # 程序头的 magic（ISA v0.3）
    assert len(prog) == 16 + 2 * 136

    dumps = {d["addr"]: d["bytes"] for d in spec["dumps"]}
    assert dumps[c.addr] == 16


def test_collect_ok_and_mismatch(tmp_path):
    t, _ = _small_test()
    pkg = t.write_package(tmp_path / "pkg")
    good = np.arange(8, dtype=np.float16).tobytes()
    _fake_gem5(pkg, out=good)
    r = RunResult.collect(t, pkg)
    assert r.ok and r.device_cycles == 5.0

    bad = np.arange(8, dtype=np.float16)
    bad[3] = 99
    _fake_gem5(pkg, out=bad.tobytes())
    r = RunResult.collect(t, pkg)
    assert not r.ok
    assert r.mismatches[0].count == 1 and r.mismatches[0].first[0][0] == 3


def test_nan_matches_any_payload(tmp_path):
    t = OpTest("nan")
    c = t.sram(0, (2,), "FP32")
    t.add(0, asm.end())
    t.expect("c", c, np.array([np.nan, 1.0], np.float32))
    pkg = t.write_package(tmp_path / "pkg")
    other_nan = np.array([0x7FC00001, 0x3F800000], np.uint32).view(np.float32)
    _fake_gem5(pkg, out=other_nan.tobytes())
    assert RunResult.collect(t, pkg).ok


def test_local_tensor_needs_tile_for_backdoor(tmp_path):
    t = OpTest("local")
    s = t.local((16,), "INT8")
    assert addrmap.is_local(s.addr)
    with pytest.raises(ValueError):
        t.load(s, np.zeros(16, np.int8))
    t.load(s, np.arange(16, dtype=np.int8), tile=[1, 7])
    t.expect("s7", s, np.arange(16, dtype=np.int8), tile=7)
    t.add([1, 7], asm.end())
    spec = json.loads((t.write_package(tmp_path / "pkg") / "package.json").read_text())
    off = s.addr - addrmap.local_sram_base()
    want = {addrmap.tile_sram_base(1) + off, addrmap.tile_sram_base(7) + off}
    assert want <= {l["addr"] for l in spec["loads"]}
    assert spec["dumps"][0]["addr"] == addrmap.tile_sram_base(7) + off


def test_gem5_runner_prefers_native(tmp_path, monkeypatch):
    """GEM5_BIN 优先；其次本平台原生构建；GW100_GEM5_DOCKER=1 或没有原生构建时走 Docker。"""
    from gw100_testkit import harness
    native = tmp_path / "gem5.opt"
    native.write_text("")
    monkeypatch.setattr(harness, "NATIVE_GEM5_BIN", {"darwin": native, "linux": native})
    monkeypatch.setattr(harness, "_docker_image", lambda: "gem5-dev")
    monkeypatch.delenv("GEM5_BIN", raising=False)
    monkeypatch.delenv("GW100_GEM5_DOCKER", raising=False)

    assert harness.gem5_runner() == ("native", native)
    assert harness.gem5_command(tmp_path)[0] == str(native)

    monkeypatch.setenv("GW100_GEM5_DOCKER", "1")
    assert harness.gem5_runner() == ("docker", None)
    assert harness.gem5_command(tmp_path)[:2] == ["docker", "run"]
    monkeypatch.delenv("GW100_GEM5_DOCKER")

    other = tmp_path / "other.opt"
    monkeypatch.setenv("GEM5_BIN", str(other))
    assert harness.gem5_runner() == ("native", other)
    monkeypatch.delenv("GEM5_BIN")

    native.unlink()
    assert harness.gem5_runner() == ("docker", None)
