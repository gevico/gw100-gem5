# 功能核心库的独立构建，只用于 L0 测试（gem5 内的构建见 gem5-ext/SConscript）。
#   make            生成 build/libgw100fc.so
#   make test       运行 L0 测试，用例在仓库根目录的 tests/l0_func_core（需要根目录的 .venv）

CXX ?= c++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Werror -fPIC
SRCS := src/numerics.cc src/inst.cc src/vector_ops.cc
HDRS := $(wildcard include/gw100/*.hh)
PYTHON ?= ../../.venv/bin/python

build/libgw100fc.so: $(SRCS) src/capi.cc $(HDRS)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -Iinclude -shared -o $@ $(SRCS) src/capi.cc

test: build/libgw100fc.so
	$(PYTHON) -m pytest -q ../../tests/l0_func_core

clean:
	rm -rf build

.PHONY: test clean
