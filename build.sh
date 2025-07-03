#!/bin/bash

# 添加错误处理
set -e
set -o pipefail

# 显示彩色输出
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

echo -e "${GREEN}=== 开始编译 Kama WebServer ($(date '+%Y-%m-%d %H:%M:%S')) ===${NC}"

# 确保在正确的目录中
cd "$(dirname "$0")"
PROJECT_DIR=$(pwd)

# 打印环境信息
echo -e "${YELLOW}编译环境信息:${NC}"
echo "工作目录: $PROJECT_DIR"
echo "系统: $(uname -a)"
echo "CPU 核心数: $(nproc)"
echo "可用内存: $(free -h | awk '/^Mem:/ {print $7}')"

# 确保bin目录存在
mkdir -p bin

# 检查是否已有cmake生成的文件
if [ ! -f "build/Makefile" ]; then
    echo -e "${YELLOW}初始化构建目录...${NC}"
    mkdir -p build
    cmake -S . -B build
fi

# 检查是否需要完全重新构建
if [ "$1" == "--clean" ]; then
    echo -e "${YELLOW}执行完全重新构建...${NC}"
    rm -rf build/*
    cmake -S . -B build
else
    # 由于修复了CMakeLists.txt，我们需要清除CMakeCache.txt
    echo -e "${YELLOW}清除CMake缓存...${NC}"
    rm -f build/CMakeCache.txt
    cmake -S . -B build
fi

# 执行编译
echo -e "${YELLOW}编译所有组件...${NC}"
cmake --build build --parallel $(nproc)

# 确保二进制文件已生成
if [ -f "bin/llama_service_tcp" ] && [ -f "bin/KamaWebServer" ] && [ -f "bin/kama_http_server" ] && [ -f "bin/kama_http_server_modular" ]; then
    echo -e "${GREEN}✅ 编译成功! 所有组件已生成${NC}"
    
    # 打印生成的二进制文件信息
    echo -e "\n${YELLOW}生成的二进制文件:${NC}"
    ls -lh bin/llama_service_tcp bin/KamaWebServer bin/kama_http_server bin/kama_http_server_modular
else
    echo -e "${RED}❌ 编译失败! 未生成全部所需的二进制文件${NC}"
    echo -e "${YELLOW}已生成的文件:${NC}"
    ls -la bin/
    exit 1
fi

echo -e "\n${GREEN}=== 编译完成 ($(date '+%Y-%m-%d %H:%M:%S')) ===${NC}"
