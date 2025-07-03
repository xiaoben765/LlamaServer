#!/bin/bash

# ========================================
# Kama-WebServer 服务启动脚本
# ========================================

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

echo -e "${BLUE}=== Kama-WebServer 服务启动 ===${NC}"

# 检查可执行文件
if [ ! -f "bin/main_http" ] && [ ! -f "bin/kama_http_server" ]; then
    echo -e "${RED}❌ 服务器可执行文件不存在${NC}"
    echo "尝试编译HTTP服务器..."
    make kama_http_server || {
        echo -e "${RED}❌ 编译失败. 请手动编译项目:${NC}"
        echo "  mkdir -p build && cd build"
        echo "  cmake .. && make -j$(nproc)"
        exit 1
    }
fi

# 编译LLaMA服务
if [ ! -f "bin/llama_service" ]; then
    echo -e "${YELLOW}LLaMA服务可执行文件不存在, 尝试编译...${NC}"
    make llama_service || echo -e "${YELLOW}⚠ LLaMA服务编译失败，将使用模拟服务${NC}"
fi

# 检查端口占用
check_port() {
    local port=$1
    if lsof -Pi :$port -sTCP:LISTEN -t >/dev/null 2>&1; then
        return 0  # 端口被占用
    else
        return 1  # 端口空闲
    fi
}

# 查找可用端口
find_available_port() {
    for port in 8080 8081 8082 3000 3001; do
        if ! check_port $port; then
            echo $port
            return
        fi
    done
    echo "8080"  # 默认返回8080
}

# 停止已运行的服务器
stop_existing_servers() {
    echo -e "${YELLOW}检查并停止已运行的服务器...${NC}"
    
    # 查找并停止现有的服务器进程
    local pids=$(pgrep -f "kama_http_server\|main_http" 2>/dev/null || true)
    if [ ! -z "$pids" ]; then
        echo "停止现有服务器进程: $pids"
        kill $pids 2>/dev/null || true
        sleep 2
        
        # 强制停止如果还在运行
        local remaining_pids=$(pgrep -f "kama_http_server\|main_http" 2>/dev/null || true)
        if [ ! -z "$remaining_pids" ]; then
            echo "强制停止进程: $remaining_pids"
            kill -9 $remaining_pids 2>/dev/null || true
        fi
    fi
}

# 初始化数据库
setup_database() {
    echo -e "${CYAN}初始化数据库...${NC}"
    
    # 检查MySQL是否运行
    if ! systemctl is-active --quiet mysql; then
        echo -e "${YELLOW}⚠ MySQL未运行，尝试启动...${NC}"
        sudo systemctl start mysql || {
            echo -e "${RED}❌ 无法启动MySQL服务${NC}"
            return 1
        }
    fi
    
    # 检查数据库是否存在
    if ! mysql -u root -ppassword -e "USE kama_llm;" 2>/dev/null; then
        echo -e "${YELLOW}创建数据库 kama_llm...${NC}"
        mysql -u root -ppassword -e "CREATE DATABASE IF NOT EXISTS kama_llm CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;" || {
            echo -e "${RED}❌ 创建数据库失败${NC}"
            return 1
        }
        
        # 初始化数据库表
        echo -e "${YELLOW}初始化数据库表...${NC}"
        
        # 用户表
        mysql -u root -ppassword kama_llm -e "
        CREATE TABLE IF NOT EXISTS users (
            user_id VARCHAR(36) PRIMARY KEY,
            username VARCHAR(50) UNIQUE,
            password VARCHAR(100),
            email VARCHAR(100),
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            last_login TIMESTAMP NULL
        );"
        
        # 会话表
        mysql -u root -ppassword kama_llm -e "
        CREATE TABLE IF NOT EXISTS sessions (
            session_id VARCHAR(36) PRIMARY KEY,
            user_id VARCHAR(36),
            session_name VARCHAR(100),
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            last_active TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE
        );"
        
        # 对话表
        mysql -u root -ppassword kama_llm -e "
        CREATE TABLE IF NOT EXISTS conversations (
            message_id VARCHAR(36) PRIMARY KEY,
            session_id VARCHAR(36),
            message_type ENUM('user', 'ai'),
            content TEXT,
            timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            FOREIGN KEY (session_id) REFERENCES sessions(session_id) ON DELETE CASCADE
        );"
        
        # 缓存表
        mysql -u root -ppassword kama_llm -e "
        CREATE TABLE IF NOT EXISTS cache (
            query_hash VARCHAR(64) PRIMARY KEY,
            query TEXT,
            response TEXT,
            hits INT DEFAULT 1,
            last_used TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        );"
        
        # 配置表
        mysql -u root -ppassword kama_llm -e "
        CREATE TABLE IF NOT EXISTS config (
            key_name VARCHAR(50) PRIMARY KEY,
            value TEXT,
            updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP
        );"
        
        # 创建测试用户
        mysql -u root -ppassword kama_llm -e "
        INSERT INTO users (user_id, username, password, email) 
        VALUES ('user-1', 'test', 'password', 'test@example.com')
        ON DUPLICATE KEY UPDATE username=username;"
        
        echo -e "${GREEN}✓ 数据库表初始化完成${NC}"
    else
        echo -e "${GREEN}✓ 数据库 kama_llm 已存在${NC}"
    fi
    
    # 验证连接
    if mysql -u root -ppassword -e "USE kama_llm; SELECT COUNT(*) FROM users;" &>/dev/null; then
        echo -e "${GREEN}✓ 数据库连接正常${NC}"
        return 0
    else
        echo -e "${RED}❌ 数据库连接异常${NC}"
        return 1
    fi
}

# 启动LLaMA服务
start_llama_service() {
    echo -e "${CYAN}启动LLaMA服务...${NC}"
    
    # 检查服务是否已经在运行
    if lsof -Pi :8899 -sTCP:LISTEN -t >/dev/null 2>&1; then
        echo -e "${YELLOW}LLaMA服务已经在运行 (端口 8899)${NC}"
        return
    fi
    
    # 检查可执行文件
    if [ ! -x "bin/llama_service" ]; then
        echo -e "${YELLOW}LLaMA服务可执行文件不存在，将使用模拟服务${NC}"
        return 1
    fi
    
    # 查找并停止现有的服务器进程
    local pids=$(pgrep -f "llama_service" 2>/dev/null || true)
    if [ ! -z "$pids" ]; then
        echo "停止现有LLaMA服务进程: $pids"
        kill $pids 2>/dev/null || true
        sleep 2
    fi
    
    # 启动服务
    echo "启动LLaMA服务 (bin/llama_service)..."
    ./bin/llama_service > logs/llama_service.log 2>&1 &
    
    # 等待服务启动
    echo "等待LLaMA服务启动..."
    for i in {1..5}; do
        if lsof -Pi :8899 -sTCP:LISTEN -t >/dev/null 2>&1; then
            echo -e "${GREEN}✓ LLaMA服务成功启动${NC}"
            return 0
        fi
        sleep 1
    done
    
    echo -e "${YELLOW}⚠ LLaMA服务似乎未正常启动${NC}"
    return 1
}

# 启动服务器
start_server() {
    local port=$(find_available_port)
    
    echo -e "${CYAN}准备在端口 $port 启动服务器...${NC}"
    
    # 确定要使用的可执行文件
    local executable=""
    if [ -f "bin/main_http" ]; then
        executable="bin/main_http"
    elif [ -f "bin/kama_http_server" ]; then
        executable="bin/kama_http_server"
    fi
    
    echo "使用可执行文件: $executable"
    
    # 设置端口环境变量（如果程序支持的话）
    export HTTP_PORT=$port
    export SERVER_PORT=$port
    
    # 启动服务器
    echo -e "${GREEN}启动服务器...${NC}"
    ./$executable &
    local server_pid=$!
    
    echo "服务器进程 ID: $server_pid"
    sleep 3
    
    # 检查服务器是否成功启动
    echo -e "${CYAN}检查服务器状态...${NC}"
    
    local started=false
    for attempt in {1..10}; do
        for check_port in 8080 8081 8082 3000; do
            if curl -s --connect-timeout 2 "http://127.0.0.1:$check_port/api/status" > /dev/null 2>&1; then
                echo -e "${GREEN}✓ 服务器成功启动在端口 $check_port${NC}"
                echo -e "${BLUE}访问地址: http://127.0.0.1:$check_port${NC}"
                echo -e "${BLUE}状态页面: http://127.0.0.1:$check_port/api/status${NC}"
                
                # 导出端口供性能测试使用
                echo "export SERVER_PORT=$check_port" > .server_port
                echo -e "${YELLOW}已保存端口信息到 .server_port 文件${NC}"
                
                started=true
                break 2
            fi
        done
        
        echo "等待服务器启动... (尝试 $attempt/10)"
        sleep 2
    done
    
    if [ "$started" = false ]; then
        echo -e "${RED}❌ 服务器启动失败或无法连接${NC}"
        echo "请检查服务器日志或手动启动"
        return 1
    fi
    
    return 0
}

# 显示服务器信息
show_server_info() {
    echo ""
    echo -e "${BLUE}=== 服务器信息 ===${NC}"
    
    # 查找运行的服务器
    local http_running=false
    for port in 8080 8081 8082 3000; do
        if curl -s --connect-timeout 2 "http://127.0.0.1:$port/api/status" > /dev/null 2>&1; then
            echo -e "${GREEN}✓ HTTP服务器运行在: http://127.0.0.1:$port${NC}"
            echo "可用的 API 端点:"
            echo "  GET  /api/status          - 服务器状态"
            echo "  POST /api/chat           - 聊天接口"
            echo "  GET  /api/sessions       - 会话列表"
            echo "  POST /api/register       - 用户注册"
            echo "  POST /api/login          - 用户登录"
            echo ""
            echo "性能测试命令:"
            echo "  SERVER_PORT=$port ./run_performance_tests.sh --quick"
            http_running=true
            break
        fi
    done
    
    if [ "$http_running" = false ]; then
        echo -e "${RED}❌ HTTP服务器未运行${NC}"
    fi
    
    # 检查LLaMA服务状态
    if lsof -Pi :8899 -sTCP:LISTEN -t >/dev/null 2>&1; then
        echo -e "${GREEN}✓ LLaMA服务正在运行 (端口 8899)${NC}"
    else
        echo -e "${YELLOW}⚠ LLaMA服务未运行 - 将使用模拟服务${NC}"
    fi
    
    # 检查数据库状态
    if mysql -u root -ppassword -e "USE kama_llm; SELECT COUNT(*) FROM users;" &>/dev/null; then
        echo -e "${GREEN}✓ 数据库连接正常${NC}"
        
        # 显示数据库统计信息
        local user_count=$(mysql -u root -ppassword -BNe "SELECT COUNT(*) FROM kama_llm.users;" 2>/dev/null)
        local session_count=$(mysql -u root -ppassword -BNe "SELECT COUNT(*) FROM kama_llm.sessions;" 2>/dev/null)
        local msg_count=$(mysql -u root -ppassword -BNe "SELECT COUNT(*) FROM kama_llm.conversations;" 2>/dev/null)
        
        echo "  - 用户数: $user_count"
        echo "  - 会话数: $session_count"
        echo "  - 消息数: $msg_count"
    else
        echo -e "${RED}❌ 数据库连接异常${NC}"
    fi
}

# 创建日志目录
create_log_dirs() {
    mkdir -p logs
}

# 主函数
main() {
    create_log_dirs
    
    case "${1:-start}" in
        "start")
            stop_existing_servers
            setup_database
            start_llama_service
            start_server
            show_server_info
            ;;
        "stop")
            stop_existing_servers
            pkill -f "llama_service" 2>/dev/null || true
            echo -e "${GREEN}所有服务已停止${NC}"
            ;;
        "restart")
            stop_existing_servers
            pkill -f "llama_service" 2>/dev/null || true
            sleep 1
            setup_database
            start_llama_service
            start_server
            show_server_info
            ;;
        "status")
            show_server_info
            ;;
        "db")
            setup_database
            ;;
        "llama")
            start_llama_service
            ;;
        *)
            echo "用法: $0 {start|stop|restart|status|db|llama}"
            echo ""
            echo "  start   - 启动所有服务 (默认)"
            echo "  stop    - 停止所有服务"
            echo "  restart - 重启所有服务"
            echo "  status  - 显示服务状态"
            echo "  db      - 仅初始化数据库"
            echo "  llama   - 仅启动LLaMA服务"
            exit 1
            ;;
    esac
}

# 运行主函数
main "$@"
