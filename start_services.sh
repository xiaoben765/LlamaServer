#!/bin/bash

# ========================================
# LLaMA WebServer 简化启动脚本
# 基于当前的模块化架构设计
# ========================================

set -e

# 解析命令行参数
FORCE_CPU=false
CUSTOM_GPU_LAYERS=""

for arg in "$@"; do
    case $arg in
        --cpu)
            FORCE_CPU=true
            shift
            ;;
        --gpu-layers=*)
            CUSTOM_GPU_LAYERS="${arg#*=}"
            shift
            ;;
        --help|-h)
            echo "用法: $0 [选项]"
            echo "选项:"
            echo "  --cpu              强制使用 CPU 模式"
            echo "  --gpu-layers=N     设置 GPU 层数 (默认: 32)"
            echo "  --help, -h         显示此帮助信息"
            exit 0
            ;;
        *)
            # 未知参数
            ;;
    esac
done

# 显示彩色输出
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# 项目路径配置
PROJECT_DIR="/home/shl203/llama-webserver"
BIN_DIR="$PROJECT_DIR/bin"
LOG_DIR="$PROJECT_DIR/logs"

# 服务配置
HTTP_SERVER="$BIN_DIR/llama_http_server"
LLAMA_TCP_SERVER="$BIN_DIR/llama_service_tcp"
HTTP_LOG="$LOG_DIR/llama_http_server.log"
LLAMA_TCP_LOG="$LOG_DIR/llama_service_tcp.log"

echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}🚀 LLaMA WebServer 启动脚本${NC}"
echo -e "${BLUE}========================================${NC}"
echo -e "启动时间: ${CYAN}$(date '+%Y-%m-%d %H:%M:%S')${NC}"

# 检查环境
echo -e "${YELLOW}🔍 检查运行环境...${NC}"

# 切换到项目目录
cd "$PROJECT_DIR"

# 创建日志目录
mkdir -p "$LOG_DIR"

# 检查HTTP服务器文件
if [[ ! -f "$HTTP_SERVER" ]]; then
    echo -e "${YELLOW}⚠️ HTTP服务器未编译，开始编译...${NC}"
    mkdir -p build
    cd build
    cmake ..
    make -j$(nproc)
    cd ..
    
    if [[ ! -f "$HTTP_SERVER" ]]; then
        echo -e "${RED}❌ 编译失败${NC}"
        exit 1
    fi
    echo -e "${GREEN}✅ 编译完成${NC}"
else
    echo -e "${GREEN}✅ HTTP服务器已就绪${NC}"
fi

# 检查LLaMA TCP服务器文件
if [[ ! -f "$LLAMA_TCP_SERVER" ]]; then
    echo -e "${RED}❌ LLaMA TCP服务器未找到${NC}"
    exit 1
else
    echo -e "${GREEN}✅ LLaMA TCP服务器已就绪${NC}"
fi

# 检查端口占用
HTTP_PORT=8080
LLAMA_TCP_PORT=8899

if lsof -i:$HTTP_PORT > /dev/null 2>&1; then
    echo -e "${RED}❌ 端口 $HTTP_PORT 已被占用${NC}"
    echo "占用进程:"
    lsof -i:$HTTP_PORT
    exit 1
fi

if lsof -i:$LLAMA_TCP_PORT > /dev/null 2>&1; then
    echo -e "${RED}❌ 端口 $LLAMA_TCP_PORT 已被占用${NC}"
    echo "占用进程:"
    lsof -i:$LLAMA_TCP_PORT
    exit 1
fi

# 检查GPU状态
echo -e "${YELLOW}🔍 检查GPU状态...${NC}"
GPU_AVAILABLE=false
GPU_LAYERS=32
LLAMA_GPU_ARGS=""

# 如果用户指定了自定义 GPU 层数，使用它
if [[ -n "$CUSTOM_GPU_LAYERS" ]]; then
    GPU_LAYERS="$CUSTOM_GPU_LAYERS"
fi

# 如果用户强制使用 CPU 模式
if [[ "$FORCE_CPU" == "true" ]]; then
    echo -e "${YELLOW}⚠️ 用户指定使用 CPU 模式${NC}"
elif command -v nvidia-smi &> /dev/null; then
    if nvidia-smi &> /dev/null; then
        GPU_AVAILABLE=true
        LLAMA_GPU_ARGS="--gpu --gpu-layers=$GPU_LAYERS"
        echo -e "${GREEN}✅ NVIDIA GPU 可用，将使用 GPU 加速${NC}"
        nvidia-smi --query-gpu=name,memory.total --format=csv,noheader | while read line; do
            echo -e "${CYAN}   🎮 GPU: $line${NC}"
        done
    else
        echo -e "${YELLOW}⚠️ 检测到 nvidia-smi 但 GPU 不可用，使用 CPU 模式${NC}"
    fi
else
    echo -e "${YELLOW}⚠️ 未检测到 NVIDIA GPU，使用 CPU 模式${NC}"
fi

# 检查MySQL服务
echo -e "${YELLOW}🔍 检查MySQL服务...${NC}"
if systemctl is-active --quiet mysql; then
    echo -e "${GREEN}✅ MySQL服务正在运行${NC}"
else
    echo -e "${YELLOW}⚠️ MySQL服务未运行，HTTP服务器将使用内存数据库${NC}"
fi

echo -e "${GREEN}✅ 环境检查完成${NC}"

# 启动HTTP服务器
echo -e "${BLUE}========================================${NC}"
echo -e "${BLUE}🚀 启动服务${NC}"
echo -e "${BLUE}========================================${NC}"

# 清理旧日志
> "$HTTP_LOG"
> "$LLAMA_TCP_LOG"

# 先启动LLaMA TCP服务
echo -e "${YELLOW}🚀 启动LLaMA TCP服务...${NC}"
echo -e "${CYAN}   🌐 监听端口: $LLAMA_TCP_PORT${NC}"
echo -e "${CYAN}   🤖 模型路径: /home/shl203/llama.cpp/models/qwen/Qwen-7B-Chat.Q4_K_M.gguf${NC}"
if [[ "$GPU_AVAILABLE" == "true" ]]; then
    echo -e "${CYAN}   🎮 GPU模式: 启用 (层数: $GPU_LAYERS)${NC}"
else
    echo -e "${CYAN}   🖥️  CPU模式: 启用${NC}"
fi

"$LLAMA_TCP_SERVER" $LLAMA_GPU_ARGS > "$LLAMA_TCP_LOG" 2>&1 &
LLAMA_TCP_PID=$!

echo -e "${GREEN}✅ LLaMA TCP服务已启动 (PID: $LLAMA_TCP_PID)${NC}"

# 等待LLaMA服务启动
echo -e "${YELLOW}⏳ 等待LLaMA服务初始化...${NC}"
sleep 10

# 检查LLaMA服务状态
if ! ps -p $LLAMA_TCP_PID > /dev/null; then
    echo -e "${RED}❌ LLaMA TCP服务启动失败${NC}"
    echo "错误日志:"
    tail -20 "$LLAMA_TCP_LOG"
    exit 1
fi

# 检查LLaMA端口监听
if netstat -tlnp 2>/dev/null | grep -q ":$LLAMA_TCP_PORT.*LISTEN"; then
    echo -e "${GREEN}✅ LLaMA TCP服务正在监听端口 $LLAMA_TCP_PORT${NC}"
else
    echo -e "${YELLOW}⚠️ LLaMA端口监听检查失败，但进程运行正常${NC}"
fi

# 启动HTTP服务器
echo -e "${YELLOW}🚀 启动HTTP服务器...${NC}"
echo -e "${CYAN}   📁 静态文件目录: ./static${NC}"
echo -e "${CYAN}   🌐 监听端口: $HTTP_PORT${NC}"
echo -e "${CYAN}   🗄️ 数据库: MySQL/内存数据库${NC}"
echo -e "${CYAN}   🔗 LLaMA服务: 127.0.0.1:$LLAMA_TCP_PORT${NC}"

# 启动HTTP服务器
"$HTTP_SERVER" > "$HTTP_LOG" 2>&1 &
HTTP_PID=$!

echo -e "${GREEN}✅ HTTP服务器已启动 (PID: $HTTP_PID)${NC}"

# 等待服务器启动
echo -e "${YELLOW}⏳ 等待服务器初始化...${NC}"
sleep 5

# 检查服务器状态
if ! ps -p $HTTP_PID > /dev/null; then
    echo -e "${RED}❌ HTTP服务器启动失败${NC}"
    echo "错误日志:"
    tail -20 "$HTTP_LOG"
    exit 1
fi

# 检查端口监听
if netstat -tlnp 2>/dev/null | grep -q ":$HTTP_PORT.*LISTEN"; then
    echo -e "${GREEN}✅ HTTP服务器正在监听端口 $HTTP_PORT${NC}"
else
    echo -e "${YELLOW}⚠️ 端口监听检查失败，但进程运行正常${NC}"
fi

# 显示启动完成信息
echo -e "${BLUE}========================================${NC}"
echo -e "${GREEN}🎉 服务启动完成！${NC}"
echo -e "${BLUE}========================================${NC}"

echo -e "${CYAN} 访问地址:${NC}"
echo -e "   🏠 主界面: ${GREEN}http://localhost:$HTTP_PORT/${NC}"
echo -e "   ⚙️  管理控制台: ${GREEN}http://localhost:$HTTP_PORT/admin.html${NC}"
echo -e "   📊 API状态: ${GREEN}http://localhost:$HTTP_PORT/api/status${NC}"

echo -e "\n${CYAN}📝 日志文件:${NC}"
echo -e "   📄 HTTP服务器: $HTTP_LOG"
echo -e "   🤖 LLaMA TCP服务: $LLAMA_TCP_LOG"

echo -e "\n${YELLOW}💡 提示:${NC}"
echo -e "   • 按 ${CYAN}Ctrl+C${NC} 停止服务"
echo -e "   • 实时日志: ${CYAN}tail -f $HTTP_LOG${NC}"
echo -e "   • 停止服务: ${CYAN}./stop_services.sh${NC}"

echo -e "${BLUE}========================================${NC}"

# 清理函数
cleanup() {
    echo -e "\n${YELLOW}🛑 正在停止服务...${NC}"
    
    if [[ -n "$HTTP_PID" ]] && ps -p $HTTP_PID > /dev/null; then
        echo -e "${YELLOW}   停止HTTP服务器 (PID: $HTTP_PID)...${NC}"
        kill $HTTP_PID 2>/dev/null
        sleep 2
        if ps -p $HTTP_PID > /dev/null; then
            kill -9 $HTTP_PID 2>/dev/null
        fi
    fi
    
    if [[ -n "$LLAMA_TCP_PID" ]] && ps -p $LLAMA_TCP_PID > /dev/null; then
        echo -e "${YELLOW}   停止LLaMA TCP服务 (PID: $LLAMA_TCP_PID)...${NC}"
        kill $LLAMA_TCP_PID 2>/dev/null
        sleep 2
        if ps -p $LLAMA_TCP_PID > /dev/null; then
            kill -9 $LLAMA_TCP_PID 2>/dev/null
        fi
    fi
    
    echo -e "${GREEN}✅ 服务已停止${NC}"
    exit 0
}

# 设置信号处理
trap cleanup INT TERM

# 保持脚本运行
echo -e "${CYAN}🔄 服务运行中，按Ctrl+C停止...${NC}"
while true; do
    sleep 10
    
    # 检查LLaMA TCP服务
    if ! ps -p $LLAMA_TCP_PID > /dev/null; then
        echo -e "${RED}❌ LLaMA TCP服务意外停止！${NC}"
        echo "最近的错误日志:"
        tail -10 "$LLAMA_TCP_LOG"
        break
    fi
    
    # 检查HTTP服务器
    if ! ps -p $HTTP_PID > /dev/null; then
        echo -e "${RED}❌ HTTP服务器意外停止！${NC}"
        echo "最近的错误日志:"
        tail -10 "$HTTP_LOG"
        break
    fi
done

# 服务意外停止，执行清理
cleanup
