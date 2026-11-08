"""
@file  vscode_launch.py
@brief 生成模型与裸机负载联合调试用的 .vscode/launch.json 和 tasks.json

由 make vscode-config 调用。生成三项配置，都用 C/C++ 扩展（cpptools）的
cppdbg 调试器：
- "GW100 模型（gem5，lldb-mi）"：MIMode lldb，用 cpptools 自带的 lldb-mi
  启动 gem5，调试模型的 C++ 代码；gem5 带 --listener-mode=on，配置脚本带
  --wait-gdb，仿真开始前等待 GDB；
- "GW100 负载（裸机，aarch64 gdb）"：MIMode gdb，用 aarch64 gdb 连接 gem5
  的远程 GDB（127.0.0.1:7100），调试裸机程序；
- "GW100 联合调试"：同时启动以上两项。

macOS 上模型一侧只能用 lldb-mi：GDB 不支持在 Apple Silicon 上调试本机进程。

要调试的 test 由工作区设置 gw100.debugTest 指定；负载一侧连上之后在
gw100.debugEntry 指定的函数（默认 test_main，即每个 test 的入口）设临时
断点，连上后继续运行，因此默认停在 test 的入口。负载一侧先运行
"GW100 等待 gem5 的远程 GDB" 任务：用 lsof 检查 gem5 进程是否已在监听
该端口，而不是试连；gem5 会把第一个连入的连接当作 GDB，试连会占掉它。
端口不用 gem5 默认的 7000，连接地址写 127.0.0.1 而不是 localhost：macOS
的"隔空播放接收器"监听 *:7000，localhost 解析到 ::1 时会连到它上面。

只替换名字以 "GW100 " 开头的条目，保留用户自己的其他配置。文件带注释或
末尾逗号（JSONC）时不改写，只提示手动处理。
"""

import argparse
import json
import pathlib
import sys

PREFIX = "GW100 "
MODEL = PREFIX + "模型（gem5，lldb-mi）"
WORKLOAD = PREFIX + "负载（裸机，aarch64 gdb）"
COMPOUND = PREFIX + "联合调试"
WAIT_TASK = PREFIX + "等待 gem5 的远程 GDB"
TEST_SETTING = "gw100.debugTest"
ENTRY_SETTING = "gw100.debugEntry"
GDB_PORT = 7100


def load(path: pathlib.Path) -> dict | None:
    """读取 JSON 文件；不存在时返回空字典，不是纯 JSON 时返回 None。"""
    if not path.exists():
        return {}
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as e:
        print(
            f"[WARN] {path} 不是纯 JSON（{e}），可能带注释或末尾逗号，"
            + f"未修改；请手动加入以 \"{PREFIX}\" 开头的条目",
            file=sys.stderr,
        )
        return None


def save(path: pathlib.Path, data: dict) -> None:
    """以缩进 4 格的 JSON 写回 path。"""
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(data, indent=4, ensure_ascii=False) + "\n", encoding="utf-8"
    )


def merge(items: list, key: str, ours: list) -> list:
    """去掉 items 中 key 以 PREFIX 开头的条目，再追加 ours。"""
    kept = [i for i in items if not str(i.get(key, "")).startswith(PREFIX)]
    return kept + ours


def configurations(gem5: str, elf_dir: str, gdb: str) -> list:
    """左右两个调试配置。"""
    test = "${config:" + TEST_SETTING + "}"
    elf = "${workspaceFolder}/" + elf_dir + "/" + test + ".elf"
    model = {
        "name": MODEL,
        "type": "cppdbg",
        "request": "launch",
        "program": "${workspaceFolder}/gem5/" + gem5,
        "args": [
            "--outdir=m5out/debug",
            "--listener-mode=on",
            "${workspaceFolder}/gem5-ext/configs/run_gw100.py",
            "--board", "arm_vexpress",
            "--host", "timing",
            "--device", "gw100",
            "--workload", "baremetal",
            "--kernel", elf,
            "--wait-gdb",
            "--gdb-port", str(GDB_PORT),
        ],
        "cwd": "${workspaceFolder}/gem5",
        "MIMode": "lldb",
        "externalConsole": False,
        "stopAtEntry": False,
        "setupCommands": [
            {
                "description": "gem5 用 SIGIO 处理监听套接字的事件，"
                + "lldb 默认会因它停住进程",
                "text": "process handle SIGIO -s false -n false -p true",
                "ignoreFailures": True,
            }
        ],
    }
    entry = "${config:" + ENTRY_SETTING + "}"
    workload = {
        "name": WORKLOAD,
        "type": "cppdbg",
        "request": "launch",
        "program": elf,
        "cwd": "${workspaceFolder}/baremetal",
        "MIMode": "gdb",
        "miDebuggerPath": gdb,
        "miDebuggerServerAddress": f"127.0.0.1:{GDB_PORT}",
        "stopAtConnect": False,
        "setupCommands": [
            {
                "description": "模型一侧停在断点时，这一侧可能要等很久",
                "text": "set remotetimeout 600",
                "ignoreFailures": True,
            }
        ],
        # 连上时停在 _start（启动汇编）；在 test 的入口设临时断点，
        # 连上后继续运行，停在入口
        "postRemoteConnectCommands": [
            {"description": "在 test 的入口设临时断点",
             "text": f"tbreak {entry}"}
        ],
        "preLaunchTask": WAIT_TASK,
    }
    return [model, workload]


def wait_task() -> dict:
    """等待 gem5 开始监听远程 GDB 端口的任务，最多等 300 秒。"""
    cmd = (
        "for i in $(seq 1 1500); do "
        + f"lsof -nP -a -c gem5 -iTCP:{GDB_PORT} -sTCP:LISTEN >/dev/null "
        + "2>&1 && exit 0; "
        + "sleep 0.2; done; "
        + f"echo '300 秒内 gem5 没有在 {GDB_PORT} 端口监听' >&2; exit 1"
    )
    return {
        "label": WAIT_TASK,
        "type": "shell",
        "command": cmd,
        "presentation": {"reveal": "silent"},
        "problemMatcher": [],
    }


def main() -> int:
    """生成或更新 launch.json、tasks.json，并设置要调试的 test。"""
    parser = argparse.ArgumentParser(description=__doc__)
    _ = parser.add_argument("--vscode-dir", type=pathlib.Path, required=True)
    _ = parser.add_argument("--gem5", required=True, help="相对 gem5/ 的路径")
    _ = parser.add_argument("--elf-dir", required=True, help="相对仓库根目录")
    _ = parser.add_argument(
        "--gdb", default="", help="aarch64 gdb 的路径；为空时只写命令名"
    )
    _ = parser.add_argument(
        "--test", default="", help="要调试的 test；为空时保留原有设置"
    )
    args = parser.parse_args()

    gdb: str = args.gdb
    if not gdb:
        gdb = "aarch64-linux-gnu-gdb"
        print(
            "[WARN] 没有找到 aarch64-linux-gnu-gdb 或 gdb-multiarch，"
            + f"负载一侧的 miDebuggerPath 暂写为 {gdb}；"
            + "安装后重新运行 make vscode-config",
            file=sys.stderr,
        )

    vdir: pathlib.Path = args.vscode_dir
    launch_path, tasks_path = vdir / "launch.json", vdir / "tasks.json"
    settings_path = vdir / "settings.json"

    launch = load(launch_path)
    if launch is not None:
        launch.setdefault("version", "0.2.0")
        launch["configurations"] = merge(
            launch.get("configurations", []),
            "name",
            configurations(args.gem5, args.elf_dir, gdb),
        )
        launch["compounds"] = merge(
            launch.get("compounds", []),
            "name",
            [{"name": COMPOUND, "configurations": [MODEL, WORKLOAD]}],
        )
        save(launch_path, launch)

    tasks = load(tasks_path)
    if tasks is not None:
        tasks.setdefault("version", "2.0.0")
        tasks["tasks"] = merge(tasks.get("tasks", []), "label", [wait_task()])
        save(tasks_path, tasks)

    settings = load(settings_path)
    test = args.test
    if settings is not None:
        if test or TEST_SETTING not in settings:
            settings[TEST_SETTING] = test or "hello"
        test = settings[TEST_SETTING]
        settings.setdefault(ENTRY_SETTING, "test_main")
        save(settings_path, settings)

    print(
        f"已生成联合调试配置：{launch_path}、{tasks_path}；"
        + f"调试的 test：{test}（设置项 {TEST_SETTING}），"
        + f"停在入口函数（设置项 {ENTRY_SETTING}）"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
