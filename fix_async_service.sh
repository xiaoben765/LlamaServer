#!/bin/bash

# 重新构建
echo "清理旧构建..."
make clean

# 构建测试程序
echo "开始构建测试程序..."
make test_concurrency

if [ $? -ne 0 ]; then
    echo "构建失败"
    exit 1
fi

# 确保输出可见
export CLICOLOR_FORCE=1

# 执行测试，同时重定向到日志文件
echo -e "\n===== 测试异步LLaMA服务（使用模拟服务） ====="
./bin/test_concurrency --service | tee async_service_test_log.txt

echo -e "\n测试完成！日志已保存到 async_service_test_log.txt"
