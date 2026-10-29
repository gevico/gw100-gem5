"""
@file  vscode_settings.py
@brief 为 clangd 调整工作区 .vscode/settings.json

由 make vscode-config 调用。代码提示交给 clangd，这里关闭 cpptools 的
IntelliSense，并删掉旧版 vscode-config 写入的 C_Cpp.default.* 配置，
避免两套诊断重复。

VS Code 允许 settings.json 带注释和末尾逗号（JSONC），标准库 json 解析
不了。遇到这种文件时不改写（改写会丢掉注释），只提示手动修改。
"""

import argparse
import json
import pathlib
import sys

STALE_KEYS = (
    "C_Cpp.default.configurationProvider",
    "C_Cpp.default.includePath",
    "C_Cpp.default.browse.path",
    "C_Cpp.default.compileCommands",
)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("settings", type=pathlib.Path)
    args = parser.parse_args()

    settings = {}
    if args.settings.exists():
        try:
            settings = json.loads(args.settings.read_text(encoding="utf-8"))
        except json.JSONDecodeError as e:
            print(
                f"[WARN] {args.settings} 不是纯 JSON（{e}），可能带注释或"
                "末尾逗号，未修改。\n"
                '       请手动加入 "C_Cpp.intelliSenseEngine": "disabled"，'
                f"并删除 {', '.join(STALE_KEYS)}",
                file=sys.stderr,
            )
            return 0

    for key in STALE_KEYS:
        settings.pop(key, None)
    settings["C_Cpp.intelliSenseEngine"] = "disabled"

    args.settings.parent.mkdir(parents=True, exist_ok=True)
    args.settings.write_text(
        json.dumps(settings, indent=4, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    print(f"已关闭 cpptools IntelliSense：{args.settings}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
