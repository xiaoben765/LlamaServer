#!/bin/bash

# 数据库连接信息
DB_USER="root"
DB_PASS="password"
DB_NAME="kama_llm"

# 显示彩色输出
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}=== 数据库状态统计 ===${NC}"

# 检查MySQL服务状态
if systemctl is-active --quiet mysql; then
    echo -e "${GREEN}MySQL 服务状态: 运行中${NC}"
else
    echo -e "${YELLOW}MySQL 服务状态: 未运行${NC}"
    echo "尝试启动MySQL服务..."
    sudo systemctl start mysql
    sleep 1
    if systemctl is-active --quiet mysql; then
        echo -e "${GREEN}MySQL 服务已成功启动${NC}"
    else
        echo -e "${YELLOW}无法启动MySQL服务，请检查配置${NC}"
        exit 1
    fi
fi

# 检查kama_llm数据库是否存在
DB_EXISTS=$(mysql -u$DB_USER -p$DB_PASS -e "SHOW DATABASES LIKE '$DB_NAME';" | grep -o "$DB_NAME")
if [ -z "$DB_EXISTS" ]; then
    echo -e "${YELLOW}数据库 '$DB_NAME' 不存在${NC}"
    echo -n "是否创建新数据库? (y/n): "
    read -r CREATE_DB
    
    if [[ "$CREATE_DB" == "y" || "$CREATE_DB" == "Y" ]]; then
        echo "创建数据库 $DB_NAME..."
        mysql -u$DB_USER -p$DB_PASS -e "CREATE DATABASE $DB_NAME CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;"
        echo -e "${GREEN}数据库创建成功${NC}"
        echo -e "${YELLOW}请重启HTTP服务器以初始化数据库表结构${NC}"
        exit 0
    else
        exit 1
    fi
fi

# 检查表是否存在
USERS_TABLE=$(mysql -u$DB_USER -p$DB_PASS -e "SHOW TABLES FROM $DB_NAME LIKE 'users';" | grep -o "users")
SESSIONS_TABLE=$(mysql -u$DB_USER -p$DB_PASS -e "SHOW TABLES FROM $DB_NAME LIKE 'sessions';" | grep -o "sessions")
CONVERSATIONS_TABLE=$(mysql -u$DB_USER -p$DB_PASS -e "SHOW TABLES FROM $DB_NAME LIKE 'conversations';" | grep -o "conversations")
CACHE_TABLE=$(mysql -u$DB_USER -p$DB_PASS -e "SHOW TABLES FROM $DB_NAME LIKE 'cache';" | grep -o "cache")

if [ -z "$USERS_TABLE" ] || [ -z "$SESSIONS_TABLE" ] || [ -z "$CONVERSATIONS_TABLE" ] || [ -z "$CACHE_TABLE" ]; then
    echo -e "${YELLOW}某些必要的表不存在${NC}"
    echo "用户表: ${USERS_TABLE:-缺失}"
    echo "会话表: ${SESSIONS_TABLE:-缺失}"
    echo "对话表: ${CONVERSATIONS_TABLE:-缺失}"
    echo "缓存表: ${CACHE_TABLE:-缺失}"
    echo -e "${YELLOW}请重启HTTP服务器以初始化数据库表结构${NC}"
    exit 1
fi

# 获取统计信息
USER_COUNT=$(mysql -u$DB_USER -p$DB_PASS -s -N -e "SELECT COUNT(*) FROM $DB_NAME.users;")
SESSION_COUNT=$(mysql -u$DB_USER -p$DB_PASS -s -N -e "SELECT COUNT(*) FROM $DB_NAME.sessions;")
CONV_COUNT=$(mysql -u$DB_USER -p$DB_PASS -s -N -e "SELECT COUNT(*) FROM $DB_NAME.conversations;")
CACHE_COUNT=$(mysql -u$DB_USER -p$DB_PASS -s -N -e "SELECT COUNT(*) FROM $DB_NAME.cache;")
CACHE_HITS=$(mysql -u$DB_USER -p$DB_PASS -s -N -e "SELECT SUM(hit_count) FROM $DB_NAME.cache;")
CACHE_HITS=${CACHE_HITS:-0}

# 显示数据库统计
echo -e "${BLUE}数据库名称:${NC} $DB_NAME"
echo -e "${GREEN}用户数量:${NC} $USER_COUNT"
echo -e "${GREEN}会话数量:${NC} $SESSION_COUNT"
echo -e "${GREEN}对话记录数:${NC} $CONV_COUNT"
echo -e "${GREEN}缓存条目:${NC} $CACHE_COUNT (总命中: $CACHE_HITS)"

# 显示最近活跃的用户
echo -e "\n${BLUE}最近活跃用户:${NC}"
mysql -u$DB_USER -p$DB_PASS -e "SELECT username, email, 
    FROM_UNIXTIME(last_login) as last_login,
    (SELECT COUNT(*) FROM $DB_NAME.sessions s WHERE s.user_id = u.user_id) as sessions_count
    FROM $DB_NAME.users u
    ORDER BY last_login DESC
    LIMIT 5;" --table

# 显示最近活跃的会话
echo -e "\n${BLUE}最近活跃会话:${NC}"
mysql -u$DB_USER -p$DB_PASS -e "SELECT s.session_name, u.username,
    FROM_UNIXTIME(s.last_active) as last_active,
    (SELECT COUNT(*) FROM $DB_NAME.conversations c WHERE c.session_id = s.session_id) as messages
    FROM $DB_NAME.sessions s
    LEFT JOIN $DB_NAME.users u ON s.user_id = u.user_id
    ORDER BY s.last_active DESC
    LIMIT 5;" --table

# 显示最热门的缓存条目
echo -e "\n${BLUE}最热门缓存内容:${NC}"
mysql -u$DB_USER -p$DB_PASS -e "SELECT 
    SUBSTRING(query_text, 1, 50) as query_preview,
    hit_count,
    FROM_UNIXTIME(created_at) as created,
    FROM_UNIXTIME(last_accessed) as last_used
    FROM $DB_NAME.cache
    ORDER BY hit_count DESC
    LIMIT 5;" --table

echo -e "\n${GREEN}数据库状态检查完成${NC}"
