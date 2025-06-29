#!/bin/bash

# 添加错误处理
set -o pipefail

# 显示彩色输出
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

# 确保编译后的可执行文件路径正确
LLAMA_SERVICE="/home/shl203/kama-webserver/bin/llama_service_tcp"
KAMA_WEBSERVER="/home/shl203/kama-webserver/bin/KamaWebServer"
LOG_DIR="/home/shl203/kama-webserver/logs"
LLAMA_LOG="$LOG_DIR/llama_service.log"
WEB_LOG="$LOG_DIR/webserver.log"

# 设置HTTP服务器路径
HTTP_SERVER="/home/shl203/kama-webserver/bin/kama_http_server"
HTTP_LOG="$LOG_DIR/http_server.log"

# 打印启动时间和环境信息
echo -e "${GREEN}=== 服务启动 ($(date '+%Y-%m-%d %H:%M:%S')) ===${NC}"
echo "主机: $(hostname)"
echo "用户: $(whoami)"

# CUDA 环境变量（确保 GPU 可用）
export CUDA_HOME=/usr/local/cuda-12.8
export PATH=$CUDA_HOME/bin:$PATH
export LD_LIBRARY_PATH=$CUDA_HOME/lib64:$LD_LIBRARY_PATH

# 检查MySQL是否已安装
if ! command -v mysql &> /dev/null; then
    echo -e "${RED}MySQL客户端未安装，HTTP服务器可能无法正常工作${NC}"
    echo "提示: 运行 sudo apt install mysql-server mysql-client 安装MySQL"
fi

# 创建日志目录
mkdir -p $LOG_DIR

if [[ ! -f "$LLAMA_SERVICE" ]]; then
    echo "LLaMA 服务文件不存在: $LLAMA_SERVICE"
    exit 1
fi

if [[ ! -f "$KAMA_WEBSERVER" ]]; then
    echo "WebServer 文件不存在: $KAMA_WEBSERVER"
    exit 1
fi

# 检查端口是否被占用
if lsof -i:8899 > /dev/null; then
    echo "端口 8899 已被占用，无法启动 LLaMA 服务"
    exit 1
fi

# GPU 设置（默认为 CPU）
USE_GPU=false
GPU_LAYERS=0

# 读取命令行参数（--gpu 表示启用 GPU）
for arg in "$@"; do
    case $arg in
        --gpu)
            USE_GPU=true
            ;;
        --gpu-layers=*)
            GPU_LAYERS="${arg#*=}"
            ;;
        *)
            ;;
    esac
done

# 检查 GPU 是否可用（增强版）
if $USE_GPU; then
    echo "检查GPU可用性..."
    if ! nvidia-smi > /dev/null 2>&1; then
        echo "⚠️ GPU 模式启用，但无法检测到 GPU。请确保 NVIDIA 驱动和 CUDA 已正确安装。"
        echo "⚠️ 将回退到 CPU 模式运行。"
        USE_GPU=false
    else
        nvidia_output=$(nvidia-smi --query-gpu=name,memory.total --format=csv,noheader)
        echo "✅ 检测到可用GPU: $nvidia_output"
        
        # 设置环境变量确保其他服务能访问GPU
        export CUDA_VISIBLE_DEVICES=0
        # 为HTTP服务器和WebServer添加GPU环境变量
        export USE_CUDA=1
        export CUDA_DEVICE=0
    fi
fi

# 启动 LLaMA TCP 服务（直接调用 C++ 服务）
echo "启动 LLaMA TCP 服务..."
if [[ "$USE_GPU" == true ]]; then
    echo "🚀 启用 GPU 模式，层数: $GPU_LAYERS"
    $LLAMA_SERVICE --gpu --gpu-layers=$GPU_LAYERS > $LLAMA_LOG 2>&1 &
else
    echo "🚀 使用 CPU 模式"
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
echo "启动 WebServer..."
# 检查是否已有WebServer实例正在运行
if pgrep -f "KamaWebServer" > /dev/null; then
    echo "⚠️ 检测到WebServer已经在运行，跳过启动步骤"
    WEBSERVER_PID=$(pgrep -f "KamaWebServer")
    echo "使用已存在的WebServer进程 (PID: $WEBSERVER_PID)"
else
    # 启动新的WebServer实例
    $KAMA_WEBSERVER > $WEB_LOG 2>&1 &
    WEBSERVER_PID=$!
    echo "WebServer (PID: $WEBSERVER_PID) 已启动"

    # 检查 WebServer 是否正常启动
    sleep 2
    if ! ps -p $WEBSERVER_PID > /dev/null; then
        echo "⚠️ WebServer 启动失败，检查日志："
        tail -10 $WEB_LOG
        echo "继续启动其他服务..."
        # 注意：允许脚本继续执行，即使WebServer启动失败
    elif netstat -tulpn 2>/dev/null | grep -q ":8080"; then
        echo "✅ WebServer 启动成功，监听在端口 8080"
    else
        echo "⚠️ WebServer 进程存在，但未检测到端口监听，可能存在问题"
    fi
fi

# 启动HTTP服务器（增强版）
echo "启动 HTTP 服务器..."
# 先确保MySQL服务正在运行
if ! systemctl is-active --quiet mysql; then
    echo "⚠️ MySQL服务未运行，尝试启动MySQL..."
    sudo systemctl start mysql
    sleep 2
    if ! systemctl is-active --quiet mysql; then
        echo "❌ 无法启动MySQL服务，HTTP服务器可能无法正常工作"
    else
        echo "✅ MySQL服务已启动"
    fi
fi

# 使用更详细的日志记录方式启动HTTP服务器
$HTTP_SERVER > $HTTP_LOG 2>&1 &
HTTP_PID=$!
echo "HTTP 服务器 (PID: $HTTP_PID) 已启动，等待确认..."

# 增加等待时间并检查端口是否正在监听
sleep 3
if ! ps -p $HTTP_PID > /dev/null; then
    echo "❌ 无法启动 HTTP 服务器，进程已退出，检查日志："
    cat $HTTP_LOG
    exit 1
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
echo -e "🔗 LLaMA TCP 服务（端口8899）- 日志：$LLAMA_LOG"

if ps -p $WEBSERVER_PID > /dev/null; then
    echo -e "🌐 WebServer（端口8080）- 日志：$WEB_LOG"
else
    echo -e "${YELLOW}⚠️ WebServer未运行${NC}"
fi

if ps -p $HTTP_PID > /dev/null; then
    echo -e "${GREEN}🌍 HTTP 服务器正在运行（端口8081）${NC}"
    echo -e "   访问: http://localhost:8081/"
    echo -e "   API状态: http://localhost:8081/api/status"
    echo -e "   日志: $HTTP_LOG" 
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
