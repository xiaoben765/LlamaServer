#!/bin/bash

# 数据库连接信息
DB_USER="root"
DB_PASS="password"
DB_NAME="kama_llm"

# 显示彩色输出
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 显示菜单
echo -e "${BLUE}=== 数据表清空工具 ===${NC}"
echo "请选择要清空的表:"
echo "1) 清空对话记录表 (conversations)"
echo "2) 清空会话表 (sessions)"
echo "3) 清空缓存表 (cache)"
echo "4) 清空用户表 (users)"
echo "5) 清空所有表"
echo "0) 退出"

echo -n "请输入选项 [0-5]: "
read -r CHOICE

case $CHOICE in
    1)
        echo -e "${YELLOW}警告：即将清空所有对话记录${NC}"
        echo -n "确定要继续吗？(y/n): "
        read -r CONFIRM
        
        if [[ "$CONFIRM" == "y" || "$CONFIRM" == "Y" ]]; then
            echo -e "${YELLOW}清空对话记录表...${NC}"
            mysql -u$DB_USER -p$DB_PASS -e "TRUNCATE TABLE $DB_NAME.conversations;"
            echo -e "${GREEN}对话记录已清空!${NC}"
        else
            echo "操作已取消"
        fi
        ;;
        
    2)
        echo -e "${YELLOW}警告：即将清空所有会话记录${NC}"
        echo -e "${RED}注意：这将同时删除所有对话记录，因为它们关联到会话!${NC}"
        echo -n "确定要继续吗？(y/n): "
        read -r CONFIRM
        
        if [[ "$CONFIRM" == "y" || "$CONFIRM" == "Y" ]]; then
            echo -e "${YELLOW}清空对话记录表...${NC}"
            mysql -u$DB_USER -p$DB_PASS -e "TRUNCATE TABLE $DB_NAME.conversations;"
            
            echo -e "${YELLOW}清空会话表...${NC}"
            mysql -u$DB_USER -p$DB_PASS -e "TRUNCATE TABLE $DB_NAME.sessions;"
            
            echo -e "${GREEN}会话和对话记录已清空!${NC}"
        else
            echo "操作已取消"
        fi
        ;;
        
    3)
        echo -e "${YELLOW}警告：即将清空所有缓存数据${NC}"
        echo -n "确定要继续吗？(y/n): "
        read -r CONFIRM
        
        if [[ "$CONFIRM" == "y" || "$CONFIRM" == "Y" ]]; then
            echo -e "${YELLOW}清空缓存表...${NC}"
            mysql -u$DB_USER -p$DB_PASS -e "TRUNCATE TABLE $DB_NAME.cache;"
            echo -e "${GREEN}缓存数据已清空!${NC}"
        else
            echo "操作已取消"
        fi
        ;;
        
    4)
        echo -e "${RED}警告：即将清空所有用户数据${NC}"
        echo -e "${RED}这将同时删除所有会话和对话记录!${NC}"
        echo -n "确定要继续吗？(y/n): "
        read -r CONFIRM
        
        if [[ "$CONFIRM" == "y" || "$CONFIRM" == "Y" ]]; then
            echo -e "${YELLOW}清空对话记录表...${NC}"
            mysql -u$DB_USER -p$DB_PASS -e "TRUNCATE TABLE $DB_NAME.conversations;"
            
            echo -e "${YELLOW}清空会话表...${NC}"
            mysql -u$DB_USER -p$DB_PASS -e "TRUNCATE TABLE $DB_NAME.sessions;"
            
            echo -e "${YELLOW}清空用户表...${NC}"
            mysql -u$DB_USER -p$DB_PASS -e "TRUNCATE TABLE $DB_NAME.users;"
            
            echo -e "${GREEN}用户数据已清空!${NC}"
        else
            echo "操作已取消"
        fi
        ;;
        
    5)
        echo -e "${RED}警告：即将清空所有表的数据${NC}"
        echo -n "确定要继续吗？(y/n): "
        read -r CONFIRM
        
        if [[ "$CONFIRM" == "y" || "$CONFIRM" == "Y" ]]; then
            echo -e "${YELLOW}清空对话记录表...${NC}"
            mysql -u$DB_USER -p$DB_PASS -e "TRUNCATE TABLE $DB_NAME.conversations;"
            
            echo -e "${YELLOW}清空会话表...${NC}"
            mysql -u$DB_USER -p$DB_PASS -e "TRUNCATE TABLE $DB_NAME.sessions;"
            
            echo -e "${YELLOW}清空用户表...${NC}"
            mysql -u$DB_USER -p$DB_PASS -e "TRUNCATE TABLE $DB_NAME.users;"
            
            echo -e "${YELLOW}清空缓存表...${NC}"
            mysql -u$DB_USER -p$DB_PASS -e "TRUNCATE TABLE $DB_NAME.cache;"
            
            echo -e "${GREEN}所有表数据已清空!${NC}"
        else
            echo "操作已取消"
        fi
        ;;
        
    0)
        echo "退出"
        exit 0
        ;;
        
    *)
        echo -e "${RED}无效的选择!${NC}"
        exit 1
        ;;
esac

echo -e "${YELLOW}注意：您可能需要重启HTTP服务器以使更改生效${NC}"
echo -n "是否现在重启服务？(y/n): "
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
    echo -e "${YELLOW}请稍后手动重启HTTP服务器以应用更改${NC}"
fi
