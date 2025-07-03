#!/bin/bash

# 定义颜色变量
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # 无颜色

echo -e "${BLUE}===== 数据库优化测试 =====${NC}"

# 编译数据库优化测试程序
echo -e "${YELLOW}开始编译数据库优化测试程序...${NC}"

g++ -std=c++17 test_db_optimization.cpp src/db/DBConnectionPool.cc src/db/DBQueryHelper.cc src/db/DBIndexOptimizer.cc -I include -L/usr/lib/x86_64-linux-gnu -lmysqlclient -lpthread -o test_db_optimization

if [ $? -ne 0 ]; then
    echo -e "${RED}编译失败！${NC}"
    exit 1
fi

echo -e "${GREEN}编译成功！${NC}"

# 检查MySQL服务是否运行
echo -e "${YELLOW}检查MySQL服务状态...${NC}"
if ! systemctl is-active --quiet mysql; then
    echo -e "${YELLOW}MySQL服务未运行，尝试启动MySQL...${NC}"
    sudo systemctl start mysql
    
    if ! systemctl is-active --quiet mysql; then
        echo -e "${RED}无法启动MySQL服务，测试无法继续${NC}"
        exit 1
    fi
    
    echo -e "${GREEN}MySQL服务已启动${NC}"
else
    echo -e "${GREEN}MySQL服务正在运行${NC}"
fi

# 确保数据库存在
echo -e "${YELLOW}确保kama_llm数据库存在...${NC}"
mysql -u root -ppassword -e "CREATE DATABASE IF NOT EXISTS kama_llm;"

if [ $? -ne 0 ]; then
    echo -e "${RED}无法创建数据库，请检查MySQL连接参数${NC}"
    echo -e "${YELLOW}可能需要修改用户名和密码${NC}"
    exit 1
fi

echo -e "${GREEN}数据库准备就绪${NC}"

# 运行测试程序
echo -e "${BLUE}开始运行测试程序...${NC}"
./test_db_optimization

echo -e "${GREEN}测试完成！${NC}"
