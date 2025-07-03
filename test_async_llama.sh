#!/bin/bash

# 清理构建
echo "清理旧构建..."
make clean

# 构建测试程序
echo "构建测试程序..."
make test_concurrency

if [ $? -ne 0 ]; then
    echo "构建失败"
    exit 1
fi

# 设置输出颜色
GREEN='\033[0;32m'
RED='\033[0;31m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 仅测试异步LLaMA服务
echo -e "\n${BLUE}======== 运行异步LLaMA服务测试 ========${NC}"
./bin/test_concurrency --service 2>&1 | tee async_llama_test.log

echo -e "\n${GREEN}测试完成!${NC} 详细日志已保存到 async_llama_test.log"

# 检查测试结果
if grep -q "异步查询结果:" async_llama_test.log; then
    echo -e "${GREEN}✓ 测试成功: 成功接收到异步查询结果${NC}"
else
    echo -e "${RED}✗ 测试失败: 未收到异步查询结果${NC}"
    # 检查关键错误
    if grep -q "服务不可用" async_llama_test.log; then
        echo -e "${RED}  错误原因: 服务不可用${NC}"
    fi
    if grep -q "模型实例池为空" async_llama_test.log; then
        echo -e "${RED}  错误原因: 模型实例池为空${NC}"
    fi
    if grep -q "无法获取可用实例" async_llama_test.log; then
        echo -e "${RED}  错误原因: 无法获取可用实例${NC}"
    fi
fi
