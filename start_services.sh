#!/bin/bash

# Kama WebServer 启动脚本
# 用法:
#   ./start_services.sh              # 默认GPU模式启动
#   ./start_services.sh --cpu        # 强制CPU模式启动
#   ./start_services.sh --gpu-layers=40  # 指定GPU层数（默认32）

# 添加错误处理
set -o pipefail

# 显示彩色输出
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

# 确保编译后的可执行文件路径正确
LLAMA_SERVICE="/home/shl203/llama-webserver/bin/llama_service_tcp"
LOG_DIR="/home/shl203/llama-webserver/logs"
LLAMA_LOG="$LOG_DIR/llama_service.log"
WEB_LOG="$LOG_DIR/webserver.log"

# 设置HTTP服务器路径 - 使用统一的HTTP服务器
HTTP_SERVER="/home/shl203/llama-webserver/bin/llama_http_server"
HTTP_LOG="$LOG_DIR/llama_http_server.log"

# 打印启动时间和环境信息
echo -e "${GREEN}=== 服务启动 ($(date '+%Y-%m-%d %H:%M:%S')) ===${NC}"
echo "主机: $(hostname)"
echo "用户: $(whoami)"
echo ""

# 显示启动配置
echo -e "${YELLOW}启动配置:${NC}"
if $USE_GPU; then
    echo "  🎯 运行模式: GPU加速 (${GPU_LAYERS}层)"
else
    echo "  🎯 运行模式: CPU模式"
fi
echo "  📁 工作目录: $(pwd)"

# 检查并编译必要的二进制文件
echo -e "${YELLOW}检查并编译必要组件...${NC}"
if [[ ! -f "$LLAMA_SERVICE" || ! -f "$HTTP_SERVER" ]]; then
    echo "编译必要组件..."
    # 调用专门的编译脚本
    cd /home/shl203/llama-webserver
    bash ./build.sh
    if [ $? -ne 0 ]; then
        echo -e "${RED}❌ 编译失败，无法启动服务${NC}"
        exit 1
    fi
    echo "✅ 编译完成"
else
    echo "✅ 所有必要的二进制文件已存在，跳过编译"
fi

# CUDA 环境变量（确保 GPU 可用）
export CUDA_HOME=/usr/local/cuda-12.8
export PATH=$CUDA_HOME/bin:$PATH
export LD_LIBRARY_PATH=$CUDA_HOME/lib64:$LD_LIBRARY_PATH

# 数据库配置
DB_NAME="kama_llm"  # 更改为与代码中实际使用的数据库名一致
DB_USER="root"
DB_PASSWORD=""  # 默认为空

# 提示用户输入MySQL密码
echo -e "${YELLOW}请输入 MySQL root 用户的密码 (如无密码请直接回车):${NC}"
read -s DB_PASSWORD
echo ""

# 检查并确保 MySQL 服务正在运行
echo -e "${YELLOW}检查 MySQL 服务状态...${NC}"
if ! command -v mysql &> /dev/null; then
    echo -e "${RED}MySQL客户端未安装，HTTP服务器无法正常工作${NC}"
    echo "提示: 运行 sudo apt install mysql-server mysql-client 安装MySQL"
    exit 1
else
    echo "✅ MySQL 客户端已安装"
    
    # 确保 MySQL 服务正在运行
    if ! systemctl is-active --quiet mysql; then
        echo "⚠️ MySQL服务未运行，尝试启动MySQL..."
        sudo systemctl start mysql
        sleep 3
        if ! systemctl is-active --quiet mysql; then
            echo -e "${RED}❌ 无法启动MySQL服务，HTTP服务器无法正常工作${NC}"
            exit 1
        else
            echo "✅ MySQL服务已启动"
        fi
    else
        echo "✅ MySQL服务已在运行"
    fi
    
    # 检查数据库是否存在，如不存在则创建
    echo "检查数据库是否存在..."
    if [[ -z "$DB_PASSWORD" ]]; then
        MYSQL_CMD="mysql -u $DB_USER"
        MYSQL_CREATE_CMD="mysql -u $DB_USER -e \"CREATE DATABASE $DB_NAME CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci\""
    else
        MYSQL_CMD="mysql -u $DB_USER -p'$DB_PASSWORD'"
        MYSQL_CREATE_CMD="mysql -u $DB_USER -p'$DB_PASSWORD' -e \"CREATE DATABASE $DB_NAME CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci\""
    fi
    
    if ! eval "$MYSQL_CMD -e 'USE $DB_NAME'" 2>/dev/null; then
        echo "⚠️ 数据库 '$DB_NAME' 不存在，尝试创建..."
        if eval "$MYSQL_CREATE_CMD"; then
            echo "✅ 数据库 '$DB_NAME' 创建成功"
        else
            echo -e "${RED}❌ 创建数据库失败，请检查MySQL权限${NC}"
            exit 1
        fi
    else
        echo "✅ 数据库 '$DB_NAME' 已存在"
    fi
fi

# 创建日志目录
mkdir -p $LOG_DIR

# 创建配置目录并确保配置文件存在
CONFIG_DIR="/home/shl203/llama-webserver/config"
CONFIG_FILE="$CONFIG_DIR/config.json"
mkdir -p $CONFIG_DIR

if [[ ! -f "$CONFIG_FILE" ]]; then
    echo -e "${YELLOW}配置文件不存在，将创建默认配置文件${NC}"
    # 确保bin目录下的配置文件也存在
    mkdir -p /home/shl203/llama-webserver/bin/config
    cp -f /home/shl203/llama-webserver/config/config.json /home/shl203/llama-webserver/bin/config/ 2>/dev/null || true
fi

if [[ ! -f "$LLAMA_SERVICE" ]]; then
    echo "LLaMA 服务文件不存在: $LLAMA_SERVICE"
    exit 1
fi

if [[ ! -f "$HTTP_SERVER" ]]; then
    echo "HTTP服务器文件不存在: $HTTP_SERVER"
    exit 1
fi

# 检查端口是否被占用
if lsof -i:8899 > /dev/null; then
    echo "端口 8899 已被占用，无法启动 LLaMA 服务"
    exit 1
fi

# GPU 设置（默认启用 GPU）
USE_GPU=true
GPU_LAYERS=35

# 读取命令行参数（--cpu 表示强制使用 CPU，--gpu-layers 设置GPU层数）
for arg in "$@"; do
    case $arg in
        --cpu)
            USE_GPU=false
            ;;
        --gpu)
            USE_GPU=true
            ;;
        --gpu-layers=*)
            GPU_LAYERS="${arg#*=}"
            USE_GPU=true
            ;;
        *)
            ;;
    esac
done

# 检查 GPU 是否可用（增强版）
if $USE_GPU; then
    echo "🔍 检查GPU可用性..."
    if ! nvidia-smi > /dev/null 2>&1; then
        echo "⚠️ GPU 模式启用，但无法检测到 GPU。请确保 NVIDIA 驱动和 CUDA 已正确安装。"
        echo "⚠️ 将回退到 CPU 模式运行。"
        echo "💡 提示: 如果要强制使用CPU模式，请使用 --cpu 参数"
        USE_GPU=false
        GPU_LAYERS=0
    else
        nvidia_output=$(nvidia-smi --query-gpu=name,memory.total --format=csv,noheader)
        echo "✅ 检测到可用GPU: $nvidia_output"
        echo "🚀 GPU模式已启用，将使用 $GPU_LAYERS 层进行GPU加速"
        
        # 设置环境变量确保其他服务能访问GPU
        export CUDA_VISIBLE_DEVICES=0
        # 为HTTP服务器和WebServer添加GPU环境变量
        export USE_CUDA=1
        export CUDA_DEVICE=0
    fi
else
    echo "💻 使用CPU模式运行"
    echo "💡 提示: 如果要启用GPU加速，请使用 --gpu 参数"
fi

# 启动 LLaMA TCP 服务（直接调用 C++ 服务）
echo "🚀 启动 LLaMA TCP 服务..."
if [[ "$USE_GPU" == true ]]; then
    echo "   ⚡ GPU 加速模式 - 使用 $GPU_LAYERS 层GPU加速"
    $LLAMA_SERVICE --gpu --gpu-layers=$GPU_LAYERS > $LLAMA_LOG 2>&1 &
else
    echo "   � CPU 模式运行"
    $LLAMA_SERVICE > $LLAMA_LOG 2>&1 &
fi

LLAMA_PID=$!
echo "LLaMA TCP 服务 (PID: $LLAMA_PID) 已启动，监听端口 8899"

# 检查 LLaMA 是否正常启动
sleep 3
if ! ps -p $LLAMA_PID > /dev/null; then
    echo "无法启动 LLaMA TCP 进程，检查日志：$LLAMA_LOG"
    cat $LLAMA_LOG
    exit 1
fi

# 启动 WebServer（连接 LLaMA TCP 服务）
echo "跳过 WebServer 启动..."
# 不再启动 WebServer，因为它不能正确处理 HTTP 请求
WEBSERVER_PID=""

# 启动HTTP服务器（增强版）
echo "启动 HTTP 服务器..."
# MySQL 服务已在脚本开始时确认启动

# 已在上方处理了配置文件的创建

# 更新配置文件中的数据库密码
if [[ -f "/home/shl203/llama-webserver/config/config.json" ]]; then
    echo "更新配置文件中的数据库连接信息..."
    # 使用临时文件更新配置
    TMP_CONFIG=$(mktemp)
    cat /home/shl203/llama-webserver/config/config.json | \
    sed "s/\"db_name\": \"[^\"]*\"/\"db_name\": \"$DB_NAME\"/" | \
    sed "s/\"user\": \"[^\"]*\"/\"user\": \"$DB_USER\"/" | \
    sed "s/\"password\": \"[^\"]*\"/\"password\": \"$DB_PASSWORD\"/" > $TMP_CONFIG
    mv $TMP_CONFIG /home/shl203/llama-webserver/config/config.json
    
    # 确保bin目录下的配置文件也被更新
    if [[ -d "/home/shl203/llama-webserver/bin/config" ]]; then
        cp -f /home/shl203/llama-webserver/config/config.json /home/shl203/llama-webserver/bin/config/
        echo "✅ 同时更新了bin目录下的配置文件"
    fi
    
    echo "✅ 配置文件更新完成"
fi

# 清空之前的日志
> $HTTP_LOG

# 检查LLaMA服务是否在运行
if ps -p $LLAMA_PID > /dev/null; then
    # 测试数据库连接
    echo "测试数据库连接..."
    if ! eval "$MYSQL_CMD -e 'USE $DB_NAME; SELECT 1;'" &>/dev/null; then
        echo -e "${RED}❌ 无法连接到数据库，请检查数据库配置${NC}"
        echo "尝试重启 MySQL 服务..."
        sudo systemctl restart mysql
        sleep 3
        if ! eval "$MYSQL_CMD -e 'USE $DB_NAME; SELECT 1;'" &>/dev/null; then
            echo -e "${RED}❌ 数据库连接失败，HTTP 服务器可能无法正常工作${NC}"
            # 继续尝试启动，但提示用户可能会出现问题
        else
            echo "✅ 数据库连接成功"
        fi
    else
        echo "✅ 数据库连接成功"
    fi
    
    echo "🚀 启动HTTP服务器..."
    echo "启动命令: $HTTP_SERVER"
    echo "使用数据库: $DB_NAME, 用户: $DB_USER"
    cd /home/shl203/llama-webserver && $HTTP_SERVER > $HTTP_LOG 2>&1 &
    HTTP_PID=$!
    echo "HTTP服务器 (PID: $HTTP_PID) 已启动，等待确认..."
else
    echo "⚠️ LLaMA服务未运行，HTTP服务器将以模拟模式启动"
    echo "🚀 启动HTTP服务器（模拟LLaMA模式）..."
    echo "启动命令: $HTTP_SERVER"
    cd /home/shl203/llama-webserver && $HTTP_SERVER > $HTTP_LOG 2>&1 &
    HTTP_PID=$!
    echo "HTTP服务器 (PID: $HTTP_PID) 已启动，等待确认..."
fi

# 增加等待时间并检查端口是否正在监听
sleep 5
if ! ps -p $HTTP_PID > /dev/null; then
    echo "❌ 无法启动HTTP服务器，进程已退出，检查日志："
    cat $HTTP_LOG
    exit 1
elif netstat -tulpn 2>/dev/null | grep -q ":8080"; then
    echo "✅ HTTP服务器成功启动并监听在端口 8080"
elif netstat -tulpn 2>/dev/null | grep -q ":8081"; then
    echo "✅ HTTP 服务器成功启动并监听在端口 8081"
else
    echo "⚠️ HTTP 服务器进程存在，但未检测到端口监听，检查日志："
    tail -10 $HTTP_LOG
    # 不退出，继续运行
fi

# 记录启动状态并显示访问信息
echo -e "${GREEN}==============================================${NC}"
echo -e "${GREEN}✅ 所有服务启动完成${NC}"
if $USE_GPU; then
    echo -e "🔗 LLaMA TCP 服务（端口8899，GPU加速${GPU_LAYERS}层）- 日志：$LLAMA_LOG"
else
    echo -e "🔗 LLaMA TCP 服务（端口8899，CPU模式）- 日志：$LLAMA_LOG"
fi

if ps -p $WEBSERVER_PID > /dev/null; then
    echo -e "🌐 WebServer（端口8080）- 日志：$WEB_LOG"
else
    echo -e "${YELLOW}⚠️ WebServer未运行${NC}"
fi

if ps -p $HTTP_PID > /dev/null 2>&1; then
    echo -e "${GREEN}🌍 HTTP服务器正在运行（端口8080）${NC}"
    echo -e "   访问界面: http://localhost:8080/"
    echo -e "   状态页面: http://localhost:8080/status.html (管理工具)"
    echo -e "   API状态: http://localhost:8080/api/status"
    echo -e "   管理API: 清理缓存、数据库管理"
    echo -e "   日志: $HTTP_LOG"
    echo -e "\n${GREEN}✨ 提示: 已更新聊天界面! 现代化设计和更好的用户体验${NC}"
else
    echo -e "${YELLOW}⚠️ HTTP 服务器未正常运行${NC}"
fi

echo -e "${GREEN}==============================================${NC}"

# 改进的清理函数
cleanup() {
    echo -e "\n${YELLOW}正在停止所有服务...${NC}"
    
    if ps -p $HTTP_PID > /dev/null; then
        echo "停止HTTP服务器 (PID: $HTTP_PID)..."
        kill $HTTP_PID 2>/dev/null
    fi
    
    if ps -p $WEBSERVER_PID > /dev/null; then
        echo "停止WebServer (PID: $WEBSERVER_PID)..."
        kill $WEBSERVER_PID 2>/dev/null
    fi
    
    if ps -p $LLAMA_PID > /dev/null; then
        echo "停止LLaMA服务 (PID: $LLAMA_PID)..."
        kill $LLAMA_PID 2>/dev/null
    fi
    
    echo -e "${GREEN}所有服务已停止${NC}"
    exit 0
}

# 等待用户中断
trap cleanup INT TERM
echo "按 Ctrl+C 停止所有服务"

# 保持脚本运行，并定期检查服务状态
while true; do
    sleep 10
    # 检查服务是否仍在运行
    if ! ps -p $LLAMA_PID > /dev/null || ! ps -p $HTTP_PID > /dev/null; then
        echo -e "${YELLOW}警告: 一个或多个服务已停止运行${NC}"
    fi
done
