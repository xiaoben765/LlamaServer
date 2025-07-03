#!/bin/bash

# 设置输出颜色
GREEN='\033[0;32m'
RED='\033[0;31m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# 构建测试程序
echo -e "${BLUE}====== 构建高并发测试程序 ======${NC}"
make test_concurrency || { echo -e "${RED}构建失败${NC}"; exit 1; }
echo -e "${GREEN}构建成功!${NC}\n"

# 测试异步队列
echo -e "${BLUE}====== 测试异步任务队列 ======${NC}"
./bin/test_concurrency --async

# 测试模型实例池 (使用模拟服务)
echo -e "\n${BLUE}====== 测试模型实例池 (使用模拟服务) ======${NC}"
./bin/test_concurrency --pool

# 测试异步LLaMA服务
echo -e "\n${BLUE}====== 测试异步LLaMA服务 (使用模拟服务) ======${NC}"
./bin/test_concurrency --service

# 选择性测试高并发HTTP服务器
if [ "$1" == "--http" ]; then
    echo -e "\n${BLUE}====== 启动高并发HTTP服务器 ======${NC}"
    echo -e "${CYAN}HTTP服务器已启动，可通过 http://localhost:8080 访问${NC}"
    echo -e "${CYAN}提供的接口:${NC}"
    echo -e "${CYAN}- http://localhost:8080/hello (同步处理)${NC}"
    echo -e "${CYAN}- http://localhost:8080/async (异步处理)${NC}"
    echo -e "${CYAN}按 Ctrl+C 退出${NC}"
    ./bin/test_concurrency --http
else
    echo -e "\n${CYAN}提示: 添加 --http 参数可测试高并发HTTP服务器${NC}"
    echo -e "${CYAN}例如: ./test_concurrency.sh --http${NC}"
fi

echo -e "\n${GREEN}========== 所有测试完成! ==========${NC}"
