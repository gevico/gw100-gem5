# 功能核心库的独立构建（gem5 内的构建见 gem5-ext/SConscript）。
#   make            生成 build/libgw100fc.so
#   make clean      删除构建产物

CXX ?= c++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Werror -fPIC
SRCS := src/numerics.cc src/inst.cc src/vector_ops.cc
HDRS := $(wildcard include/gw100/*.hh)

build/libgw100fc.so: $(SRCS) src/capi.cc $(HDRS)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -Iinclude -shared -o $@ $(SRCS) src/capi.cc

clean:
	rm -rf build

.PHONY: clean
