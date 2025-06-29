#!/bin/bash

# 数据库连接信息
DB_USER="root"
DB_PASS="password"
DB_NAME="kama_llm"

# 显示彩色输出
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

echo -e "${RED}警告：即将删除并重新创建数据库 $DB_NAME${NC}"
echo -e "${YELLOW}所有数据将被永久删除！${NC}"
echo -n "确定要继续吗？(y/n): "
read -r CONFIRM

if [[ "$CONFIRM" != "y" && "$CONFIRM" != "Y" ]]; then
    echo "操作已取消"
    exit 0
fi

echo -e "${YELLOW}删除数据库 $DB_NAME...${NC}"
mysql -u$DB_USER -p$DB_PASS -e "DROP DATABASE IF EXISTS $DB_NAME;"

echo -e "${YELLOW}重新创建数据库 $DB_NAME...${NC}"
mysql -u$DB_USER -p$DB_PASS -e "CREATE DATABASE $DB_NAME CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;"

echo -e "${GREEN}数据库重置完成!${NC}"
echo -e "${YELLOW}注意：您需要重启HTTP服务器以重新初始化数据库架构${NC}"

# 提示重启HTTP服务器
echo -e "${YELLOW}是否现在重启服务？(y/n): ${NC}"
read -r RESTART

if [[ "$RESTART" == "y" || "$RESTART" == "Y" ]]; then
    echo -e "${YELLOW}正在停止当前服务...${NC}"
    pkill -f "kama_http_server"
    sleep 2
    
    echo -e "${YELLOW}启动HTTP服务器...${NC}"
    /home/shl203/kama-webserver/bin/kama_http_server > /home/shl203/kama-webserver/logs/http_server.log 2>&1 &
    HTTP_PID=$!
    echo -e "${GREEN}HTTP服务器已重启 (PID: $HTTP_PID)${NC}"
else
    echo -e "${YELLOW}请稍后手动重启HTTP服务器以初始化数据库架构${NC}"
fi
