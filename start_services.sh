#!/bin/bash

# 确保编译后的可执行文件路径正确
LLAMA_SERVICE="/home/shl203/kama-webserver/bin/llama_service_tcp"
KAMA_WEBSERVER="/home/shl203/kama-webserver/bin/KamaWebServer"
LOG_DIR="/home/shl203/kama-webserver/logs"
LLAMA_LOG="$LOG_DIR/llama_service.log"
WEB_LOG="$LOG_DIR/webserver.log"

# 设置HTTP服务器路径
HTTP_SERVER="/home/shl203/kama-webserver/bin/kama_http_server"
HTTP_LOG="$LOG_DIR/http_server.log"

# CUDA 环境变量（确保 GPU 可用）
export CUDA_HOME=/usr/local/cuda-12.8
export PATH=$CUDA_HOME/bin:$PATH
export LD_LIBRARY_PATH=$CUDA_HOME/lib64:$LD_LIBRARY_PATH

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

# 检查 GPU 是否可用
if $USE_GPU && ! nvidia-smi > /dev/null 2>&1; then
    echo "⚠️ GPU 模式启用，但无法检测到 GPU。请确保 NVIDIA 驱动和 CUDA 已正确安装。"
    USE_GPU=false
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
$KAMA_WEBSERVER > $WEB_LOG 2>&1 &
WEBSERVER_PID=$!
echo "WebServer (PID: $WEBSERVER_PID) 已启动"

# 检查 WebServer 是否正常启动
sleep 2
if ! ps -p $WEBSERVER_PID > /dev/null; then
    echo "⚠️ WebServer 启动失败，检查日志：$WEB_LOG"
    echo "继续启动其他服务..."
    # 注意：移除了 exit 1，允许脚本继续执行
else
    echo "✅ WebServer 启动成功"
fi

# 启动HTTP服务器
echo "启动 HTTP 服务器..."
$HTTP_SERVER > $HTTP_LOG 2>&1 &
HTTP_PID=$!
echo "HTTP 服务器 (PID: $HTTP_PID) 已启动"

# 检查HTTP服务器是否正常启动
sleep 2
if ! ps -p $HTTP_PID > /dev/null; then
    echo "无法启动 HTTP 服务器，检查日志：$HTTP_LOG"
    cat $HTTP_LOG
    exit 1
fi

# 更新成功启动信息
echo "============================================="
echo "✅ LLaMA TCP 服务、WebServer 和 HTTP 服务器已成功启动"
echo "🔗 LLaMA TCP 服务日志：$LLAMA_LOG"
echo "🌐 WebServer 日志：$WEB_LOG"
echo "🌍 HTTP 服务器日志：$HTTP_LOG"
echo "============================================="

# 等待用户中断
trap 'kill $LLAMA_PID $WEBSERVER_PID $HTTP_PID; echo "服务已停止"; exit 0' INT TERM
echo "按 Ctrl+C 停止服务"

# 保持脚本运行
wait
