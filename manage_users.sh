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

# 函数：显示帮助信息
show_help() {
    echo -e "${BLUE}用户和会话管理工具${NC}"
    echo "用法: $0 [选项]"
    echo ""
    echo "选项:"
    echo "  -l, --list-users        列出所有用户"
    echo "  -s, --list-sessions     列出所有会话"
    echo "  -u, --user-sessions     <用户ID>  列出指定用户的所有会话"
    echo "  -c, --clear-user        <用户ID>  删除指定用户及其所有数据"
    echo "  -cs, --clear-sessions   <用户ID>  仅清空指定用户的会话数据（保留用户信息）"
    echo "  -d, --delete-session    <会话ID>  删除特定的会话及其对话记录"
    echo "  -ca, --clear-all-cache  清空所有缓存数据"
    echo "  -h, --help              显示此帮助信息"
    echo ""
    echo "示例:"
    echo "  $0 --list-users"
    echo "  $0 --user-sessions abc123"
    echo "  $0 --clear-sessions abc123"
}

# 函数：列出所有用户
list_users() {
    echo -e "${BLUE}=== 用户列表 ===${NC}"
    mysql -u$DB_USER -p$DB_PASS -e "SELECT user_id, username, email, 
        FROM_UNIXTIME(created_at) as registered, 
        FROM_UNIXTIME(last_login) as last_login 
        FROM $DB_NAME.users ORDER BY created_at DESC;" --table

    # 显示用户总数
    USER_COUNT=$(mysql -u$DB_USER -p$DB_PASS -s -N -e "SELECT COUNT(*) FROM $DB_NAME.users;")
    echo -e "${GREEN}共有 $USER_COUNT 个用户${NC}"
}

# 函数：列出所有会话
list_sessions() {
    echo -e "${BLUE}=== 会话列表 ===${NC}"
    mysql -u$DB_USER -p$DB_PASS -e "SELECT s.session_id, s.session_name, 
        u.username, 
        FROM_UNIXTIME(s.created_at) as created, 
        FROM_UNIXTIME(s.last_active) as last_active,
        (SELECT COUNT(*) FROM $DB_NAME.conversations c WHERE c.session_id = s.session_id) as messages
        FROM $DB_NAME.sessions s
        LEFT JOIN $DB_NAME.users u ON s.user_id = u.user_id
        ORDER BY s.last_active DESC;" --table

    # 显示会话总数
    SESSION_COUNT=$(mysql -u$DB_USER -p$DB_PASS -s -N -e "SELECT COUNT(*) FROM $DB_NAME.sessions;")
    MESSAGE_COUNT=$(mysql -u$DB_USER -p$DB_PASS -s -N -e "SELECT COUNT(*) FROM $DB_NAME.conversations;")
    echo -e "${GREEN}共有 $SESSION_COUNT 个会话, $MESSAGE_COUNT 条消息${NC}"
}

# 函数：列出指定用户的会话
list_user_sessions() {
    USER_ID=$1
    
    # 验证用户存在
    USER_EXISTS=$(mysql -u$DB_USER -p$DB_PASS -s -N -e "SELECT COUNT(*) FROM $DB_NAME.users WHERE user_id='$USER_ID';")
    
    if [ "$USER_EXISTS" -eq "0" ]; then
        echo -e "${RED}错误: 用户ID '$USER_ID' 不存在${NC}"
        return 1
    fi
    
    # 获取用户信息
    USERNAME=$(mysql -u$DB_USER -p$DB_PASS -s -N -e "SELECT username FROM $DB_NAME.users WHERE user_id='$USER_ID';")
    
    echo -e "${BLUE}=== 用户 '$USERNAME' (ID: $USER_ID) 的会话列表 ===${NC}"
    mysql -u$DB_USER -p$DB_PASS -e "SELECT session_id, session_name, 
        FROM_UNIXTIME(created_at) as created, 
        FROM_UNIXTIME(last_active) as last_active,
        (SELECT COUNT(*) FROM $DB_NAME.conversations c WHERE c.session_id = s.session_id) as messages
        FROM $DB_NAME.sessions s
        WHERE user_id='$USER_ID'
        ORDER BY last_active DESC;" --table

    # 显示会话总数
    SESSION_COUNT=$(mysql -u$DB_USER -p$DB_PASS -s -N -e "SELECT COUNT(*) FROM $DB_NAME.sessions WHERE user_id='$USER_ID';")
    MESSAGE_COUNT=$(mysql -u$DB_USER -p$DB_PASS -s -N -e "
        SELECT COUNT(*) FROM $DB_NAME.conversations c 
        JOIN $DB_NAME.sessions s ON c.session_id = s.session_id 
        WHERE s.user_id='$USER_ID';")
    
    echo -e "${GREEN}用户 '$USERNAME' 共有 $SESSION_COUNT 个会话, $MESSAGE_COUNT 条消息${NC}"
}

# 函数：删除指定用户及其所有数据
clear_user() {
    USER_ID=$1
    
    # 验证用户存在
    USER_EXISTS=$(mysql -u$DB_USER -p$DB_PASS -s -N -e "SELECT COUNT(*) FROM $DB_NAME.users WHERE user_id='$USER_ID';")
    
    if [ "$USER_EXISTS" -eq "0" ]; then
        echo -e "${RED}错误: 用户ID '$USER_ID' 不存在${NC}"
        return 1
    fi
    
    # 获取用户信息
    USERNAME=$(mysql -u$DB_USER -p$DB_PASS -s -N -e "SELECT username FROM $DB_NAME.users WHERE user_id='$USER_ID';")
    
    echo -e "${RED}警告: 即将删除用户 '$USERNAME' (ID: $USER_ID) 及其所有数据${NC}"
    echo -n "确定要继续吗? (y/n): "
    read -r CONFIRM
    
    if [[ "$CONFIRM" != "y" && "$CONFIRM" != "Y" ]]; then
        echo "操作已取消"
        return 0
    fi
    
    # 删除用户的会话和对话记录
    echo -e "${YELLOW}删除用户的对话记录...${NC}"
    mysql -u$DB_USER -p$DB_PASS -e "
        DELETE c FROM $DB_NAME.conversations c 
        JOIN $DB_NAME.sessions s ON c.session_id = s.session_id 
        WHERE s.user_id='$USER_ID';"
    
    echo -e "${YELLOW}删除用户的会话...${NC}"
    mysql -u$DB_USER -p$DB_PASS -e "DELETE FROM $DB_NAME.sessions WHERE user_id='$USER_ID';"
    
    echo -e "${YELLOW}删除用户账户...${NC}"
    mysql -u$DB_USER -p$DB_PASS -e "DELETE FROM $DB_NAME.users WHERE user_id='$USER_ID';"
    
    echo -e "${GREEN}用户 '$USERNAME' 已被成功删除${NC}"
}

# 函数：清空指定用户的会话数据
clear_user_sessions() {
    USER_ID=$1
    
    # 验证用户存在
    USER_EXISTS=$(mysql -u$DB_USER -p$DB_PASS -s -N -e "SELECT COUNT(*) FROM $DB_NAME.users WHERE user_id='$USER_ID';")
    
    if [ "$USER_EXISTS" -eq "0" ]; then
        echo -e "${RED}错误: 用户ID '$USER_ID' 不存在${NC}"
        return 1
    fi
    
    # 获取用户信息
    USERNAME=$(mysql -u$DB_USER -p$DB_PASS -s -N -e "SELECT username FROM $DB_NAME.users WHERE user_id='$USER_ID';")
    
    echo -e "${YELLOW}警告: 即将清空用户 '$USERNAME' (ID: $USER_ID) 的所有会话数据${NC}"
    echo -n "确定要继续吗? (y/n): "
    read -r CONFIRM
    
    if [[ "$CONFIRM" != "y" && "$CONFIRM" != "Y" ]]; then
        echo "操作已取消"
        return 0
    fi
    
    # 删除用户的会话和对话记录
    echo -e "${YELLOW}删除用户的对话记录...${NC}"
    mysql -u$DB_USER -p$DB_PASS -e "
        DELETE c FROM $DB_NAME.conversations c 
        JOIN $DB_NAME.sessions s ON c.session_id = s.session_id 
        WHERE s.user_id='$USER_ID';"
    
    echo -e "${YELLOW}删除用户的会话...${NC}"
    mysql -u$DB_USER -p$DB_PASS -e "DELETE FROM $DB_NAME.sessions WHERE user_id='$USER_ID';"
    
    echo -e "${GREEN}用户 '$USERNAME' 的会话数据已成功清空${NC}"
}

# 函数：删除特定的会话
delete_session() {
    SESSION_ID=$1
    
    # 验证会话存在
    SESSION_EXISTS=$(mysql -u$DB_USER -p$DB_PASS -s -N -e "SELECT COUNT(*) FROM $DB_NAME.sessions WHERE session_id='$SESSION_ID';")
    
    if [ "$SESSION_EXISTS" -eq "0" ]; then
        echo -e "${RED}错误: 会话ID '$SESSION_ID' 不存在${NC}"
        return 1
    fi
    
    # 获取会话信息
    SESSION_INFO=$(mysql -u$DB_USER -p$DB_PASS -s -N -e "
        SELECT CONCAT(s.session_name, ' (用户: ', IFNULL(u.username, '未知'), ')') 
        FROM $DB_NAME.sessions s
        LEFT JOIN $DB_NAME.users u ON s.user_id = u.user_id
        WHERE s.session_id='$SESSION_ID';")
    
    echo -e "${YELLOW}警告: 即将删除会话 '$SESSION_INFO' (ID: $SESSION_ID) 及其所有对话${NC}"
    echo -n "确定要继续吗? (y/n): "
    read -r CONFIRM
    
    if [[ "$CONFIRM" != "y" && "$CONFIRM" != "Y" ]]; then
        echo "操作已取消"
        return 0
    fi
    
    # 删除会话的对话记录
    echo -e "${YELLOW}删除会话的对话记录...${NC}"
    mysql -u$DB_USER -p$DB_PASS -e "DELETE FROM $DB_NAME.conversations WHERE session_id='$SESSION_ID';"
    
    # 删除会话
    echo -e "${YELLOW}删除会话...${NC}"
    mysql -u$DB_USER -p$DB_PASS -e "DELETE FROM $DB_NAME.sessions WHERE session_id='$SESSION_ID';"
    
    echo -e "${GREEN}会话 '$SESSION_ID' 已被成功删除${NC}"
}

# 函数：清空所有缓存数据
clear_all_cache() {
    echo -e "${YELLOW}警告: 即将清空所有缓存数据${NC}"
    echo -n "确定要继续吗? (y/n): "
    read -r CONFIRM
    
    if [[ "$CONFIRM" != "y" && "$CONFIRM" != "Y" ]]; then
        echo "操作已取消"
        return 0
    fi
    
    # 获取缓存条目数量
    CACHE_COUNT=$(mysql -u$DB_USER -p$DB_PASS -s -N -e "SELECT COUNT(*) FROM $DB_NAME.cache;")
    
    # 清空缓存表
    echo -e "${YELLOW}清空缓存表...${NC}"
    mysql -u$DB_USER -p$DB_PASS -e "TRUNCATE TABLE $DB_NAME.cache;"
    
    echo -e "${GREEN}$CACHE_COUNT 条缓存数据已被成功清除${NC}"
}

# 检查必要工具
command -v mysql >/dev/null 2>&1 || { echo -e "${RED}错误: 需要mysql客户端但未安装${NC}"; exit 1; }

# 如果没有参数，显示帮助
if [ $# -eq 0 ]; then
    show_help
    exit 0
fi

# 解析命令行参数
case "$1" in
    -l|--list-users)
        list_users
        ;;
    -s|--list-sessions)
        list_sessions
        ;;
    -u|--user-sessions)
        if [ -z "$2" ]; then
            echo -e "${RED}错误: 需要提供用户ID${NC}"
            exit 1
        fi
        list_user_sessions "$2"
        ;;
    -c|--clear-user)
        if [ -z "$2" ]; then
            echo -e "${RED}错误: 需要提供用户ID${NC}"
            exit 1
        fi
        clear_user "$2"
        ;;
    -cs|--clear-sessions)
        if [ -z "$2" ]; then
            echo -e "${RED}错误: 需要提供用户ID${NC}"
            exit 1
        fi
        clear_user_sessions "$2"
        ;;
    -d|--delete-session)
        if [ -z "$2" ]; then
            echo -e "${RED}错误: 需要提供会话ID${NC}"
            exit 1
        fi
        delete_session "$2"
        ;;
    -ca|--clear-all-cache)
        clear_all_cache
        ;;
    -h|--help)
        show_help
        ;;
    *)
        echo -e "${RED}未知选项: $1${NC}"
        show_help
        exit 1
        ;;
esac

exit 0
