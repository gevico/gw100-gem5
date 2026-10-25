# GW100 顶层入口：gem5 仿真、baremetal、环境检查和格式化。
#
# 目录约定：
#   baremetal/   裸机示例源码和 Makefile，产物 build/gw100-baremetal/hello.elf
#
# 常用：
#   make check
#   make gem-build
#   make baremetal-build
#   make run
#   make format
#   make vscode-config
#   make clean

SHELL := /bin/sh
PYTHON ?= .venv/bin/python
PYTHON_CONFIG ?= $(shell command -v python3.12-config 2>/dev/null)
CLANG_FORMAT ?= clang-format

JOBS ?= $(shell sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)
GEM5_TARGET := build/ARM/gem5.opt
GEM5_CDB := build/ARM/compile_commands.json
GEM5_EXTRAS := $(abspath gem5-ext)
BAREMETAL_ELF := build/gw100-baremetal/hello.elf

VSCODE_DIR := .vscode
VSCODE_SETTINGS := $(VSCODE_DIR)/settings.json
VSCODE_PROPS := $(VSCODE_DIR)/c_cpp_properties.json
VSCODE_CDB := gem5/$(GEM5_CDB)

.DEFAULT_GOAL := help
.PHONY: all run gem-build baremetal-build vscode-config commit-check clean check format help

all: gem-build baremetal-build

run: gem-build baremetal-build
	cd gem5 && ./$(GEM5_TARGET) ../gem5-ext/configs/run_gw100.py \
		--workload baremetal --kernel $(abspath $(BAREMETAL_ELF))

gem-build:
	cd gem5 && ../.venv/bin/scons --ignore-style $(GEM5_TARGET) $(GEM5_CDB) \
		EXTRAS="$(GEM5_EXTRAS)" \
		$(if $(PYTHON_CONFIG),PYTHON_CONFIG=$(PYTHON_CONFIG),) \
		-j$(JOBS)

vscode-config: gem-build
	@test -f $(VSCODE_CDB) || { \
		echo "缺少 $(VSCODE_CDB)；请先运行 make gem-build"; \
		exit 1; \
	}
	@mkdir -p $(VSCODE_DIR)
	@$(PYTHON) -c 'import json, pathlib; \
	cdb = "$${workspaceFolder}/$(VSCODE_CDB)"; \
	sp = pathlib.Path("$(VSCODE_SETTINGS)"); \
	s = json.loads(sp.read_text(encoding="utf-8")) if sp.exists() else {}; \
	s["C_Cpp.default.compileCommands"] = cdb; \
	sp.write_text(json.dumps(s, indent=4, ensure_ascii=False) + "\n", encoding="utf-8"); \
	pp = pathlib.Path("$(VSCODE_PROPS)"); \
	p = json.loads(pp.read_text(encoding="utf-8")) if pp.exists() else {"configurations": []}; \
	cfg = p.setdefault("configurations", []) or [{}]; \
	p["configurations"] = cfg; \
	[item.__setitem__("compileCommands", cdb) for item in cfg]; \
	pp.write_text(json.dumps(p, indent=4, ensure_ascii=False) + "\n", encoding="utf-8")'
	@echo "已配置 VS Code：$(VSCODE_CDB)"

baremetal-build:
	$(MAKE) -C baremetal

commit-check:
	@echo "== 提交检查 =="
	@tools/git-hooks/pre-commit
	@tmp=$$(mktemp); \
	trap 'rm -f "$$tmp"' EXIT; \
	git log -1 --format=%B > "$$tmp"; \
	tools/git-hooks/commit-msg "$$tmp"
	@tools/git-hooks/pre-push --check-dco origin/main..HEAD

clean:
	@if [ -f baremetal/Makefile ]; then $(MAKE) -C baremetal clean; fi
	rm -rf -- build/gw100-baremetal

check:
	@echo "== GW100 环境检查 =="
	@fail=0; \
	if [ "$$(uname -s)" = "Darwin" ]; then \
		echo "[OK]   macOS $$(sw_vers -productVersion 2>/dev/null)"; \
	else \
		echo "[WARN] 当前不是 macOS；第一版只保证 macOS"; \
	fi; \
	for tool in make clang clang++ $(CLANG_FORMAT) aarch64-linux-gnu-gcc; do \
		if command -v $$tool >/dev/null 2>&1; then \
			echo "[OK]   $$tool: $$($$tool --version 2>&1 | head -1)"; \
		else \
			echo "[FAIL] 缺少 $$tool"; fail=1; \
		fi; \
	done; \
	if [ -x .venv/bin/scons ]; then \
		echo "[OK]   .venv/bin/scons"; \
	else \
		echo "[FAIL] 缺少 .venv/bin/scons"; fail=1; \
	fi; \
	if [ -x "$(PYTHON)" ]; then \
		if "$(PYTHON)" -c 'import sys; raise SystemExit(0 if sys.version_info >= (3, 12) else 1)'; then \
			echo "[OK]   $(PYTHON): $$("$(PYTHON)" --version 2>&1)"; \
		else \
			echo "[FAIL] $(PYTHON) 需要 >= 3.12"; fail=1; \
		fi; \
	else \
		echo "[FAIL] 缺少 $(PYTHON)"; fail=1; \
	fi; \
	if [ -n "$(PYTHON_CONFIG)" ] && [ -x "$(PYTHON_CONFIG)" ]; then \
		echo "[OK]   PYTHON_CONFIG=$(PYTHON_CONFIG)"; \
	else \
		echo "[WARN] 未找到 python3.12-config；scons 可能仍能自动找到 Python"; \
	fi; \
	for d in gem5 gem5-ext baremetal; do \
		if [ -d $$d ]; then echo "[OK]   $$d/"; else echo "[FAIL] 缺少 $$d/"; fail=1; fi; \
	done; \
	if [ -f baremetal/Makefile ]; then \
		echo "[OK]   baremetal/Makefile"; \
	else \
		echo "[FAIL] 缺少 baremetal/Makefile"; fail=1; \
	fi; \
	if [ -f $(VSCODE_CDB) ]; then \
		echo "[OK]   $(VSCODE_CDB)"; \
	else \
		echo "[WARN] 还没有 $(VSCODE_CDB)；运行 make gem-build 生成"; \
	fi; \
	exit $$fail

format:
	@command -v $(CLANG_FORMAT) >/dev/null 2>&1 || { \
		echo "缺少 $(CLANG_FORMAT)"; exit 1; \
	}
	@find gem5-ext -type f \
		\( -name '*.cc' -o -name '*.hh' -o -name '*.h' -o -name '*.cpp' -o -name '*.hpp' \) \
		! -path '*/build/*' ! -name 'isa_defs.hh' -print0 | \
		xargs -0 $(CLANG_FORMAT) -i --style=file:gem5/.clang-format

help:
	@echo "GW100 顶层 Makefile"
	@echo ""
	@echo "  all        构建 gem5 和 baremetal"
	@echo "  run        构建 gem5 和 baremetal，并在 gem5 中运行裸机 ELF"
	@echo "  gem-build       构建 gem5/build/ARM/gem5.opt 和 compile_commands.json"
	@echo "  baremetal-build 构建裸机 ELF（调用 baremetal/Makefile）"
	@echo "  vscode-config   生成编译数据库并写入 VS Code C/C++ 配置"
	@echo "  commit-check    检查暂存文件头、最近提交信息和 DCO"
	@echo "  check      检查 macOS、编译工具链、Python 和目录"
	@echo "  format     用 gem5/.clang-format 格式化 gem5-ext 的 C++ 文件"
	@echo "  clean      清理裸机构建产物（不删 gem5/build）"
	@echo "  help       显示本帮助"
	@echo ""
	@echo "约定：baremetal/Makefile 产物 build/gw100-baremetal/hello.elf"
