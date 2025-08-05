#!/bin/bash

# ========================================
# 快速停止所有 LLaMA WebServer 服务
# ========================================

# 显示彩色输出
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}=== LLaMA WebServer 服务停止脚本 ===${NC}"
echo "停止时间: $(date '+%Y-%m-%d %H:%M:%S')"

# 函数：安全停止进程
safe_kill_process() {
    local pid=$1
    local name=$2
    local timeout=${3:-10}
    
    if [[ -z "$pid" || "$pid" == "" ]]; then
        return 0
    fi
    
    if ! ps -p $pid > /dev/null 2>&1; then
        echo -e "${YELLOW}⚠️ $name (PID: $pid) 已经停止${NC}"
        return 0
    fi
    
    echo -e "${YELLOW}正在停止 $name (PID: $pid)...${NC}"
    
    # 首先尝试优雅停止 (SIGTERM)
    kill -TERM $pid 2>/dev/null || true
    
    # 等待进程自然退出
    local count=0
    while ps -p $pid > /dev/null 2>&1 && [ $count -lt $timeout ]; do
        sleep 1
        count=$((count + 1))
    done
    
    # 如果进程仍在运行，强制停止 (SIGKILL)
    if ps -p $pid > /dev/null 2>&1; then
        echo -e "${RED}强制停止 $name (PID: $pid)...${NC}"
        kill -KILL $pid 2>/dev/null || true
        sleep 1
    fi
    
    if ps -p $pid > /dev/null 2>&1; then
        echo -e "${RED}❌ 无法停止 $name (PID: $pid)${NC}"
        return 1
    else
        echo -e "${GREEN}✅ $name 已停止${NC}"
        return 0
    fi
}

# 函数：按端口停止服务
stop_service_by_port() {
    local port=$1
    local service_name=$2
    
    echo -e "${YELLOW}检查端口 $port 上的服务...${NC}"
    
    # 使用 lsof 查找占用端口的进程
    local pids=$(lsof -ti:$port 2>/dev/null || true)
    
    if [[ -z "$pids" ]]; then
        echo -e "${GREEN}✅ 端口 $port 没有运行的服务${NC}"
        return 0
    fi
    
    echo -e "${YELLOW}发现端口 $port 上的进程: $pids${NC}"
    
    # 获取项目目录
    local project_dir="/home/shl203/llama-webserver"
    
    for pid in $pids; do
        local process_name=$(ps -p $pid -o comm= 2>/dev/null || echo "unknown")
        local cmd=$(ps -p $pid -o cmd= 2>/dev/null || echo "")
        
        # 检查是否是项目相关的进程
        if [[ "$cmd" == *"$project_dir"* ]] || [[ "$cmd" == *"llama_http_server"* ]] || [[ "$cmd" == *"llama_service"* ]]; then
            echo -e "${YELLOW}停止项目进程: $process_name (PID: $pid)${NC}"
            echo -e "${BLUE}  命令: $(echo $cmd | cut -c1-80)${NC}"
            safe_kill_process $pid "$service_name ($process_name)" 10
        else
            echo -e "${BLUE}端口 $port 上的进程不是项目相关，跳过: $process_name (PID: $pid)${NC}"
            echo -e "${BLUE}  命令: $(echo $cmd | cut -c1-80)${NC}"
        fi
    done
}

# 函数：按进程名停止服务
stop_service_by_name() {
    local pattern=$1
    local service_name=$2
    
    echo -e "${YELLOW}查找匹配模式 '$pattern' 的进程...${NC}"
    
    # 获取项目目录
    local project_dir="/home/shl203/llama-webserver"
    
    # 使用更精确的搜索，避免误杀系统进程
    local pids=""
    
    # 首先查找项目bin目录下的进程
    pids=$(pgrep -f "${project_dir}/bin/${pattern}" 2>/dev/null || true)
    
    # 如果没找到，查找当前目录下的进程
    if [[ -z "$pids" ]]; then
        pids=$(pgrep -f "./${pattern}" 2>/dev/null || true)
    fi
    
    # 如果还没找到，在项目目录范围内查找
    if [[ -z "$pids" ]]; then
        # 获取所有匹配模式的进程，然后过滤出与项目相关的
        local all_pids=$(pgrep -f "$pattern" 2>/dev/null || true)
        for pid in $all_pids; do
            local cmd=$(ps -p $pid -o cmd= 2>/dev/null || echo "")
            # 只包含明确与项目相关的进程
            if [[ "$cmd" == *"$project_dir"* ]] || [[ "$cmd" == "./${pattern}"* ]] || [[ "$cmd" == "${project_dir}/bin/${pattern}"* ]]; then
                pids="$pids $pid"
            fi
        done
        pids=$(echo $pids | xargs)  # 清理空格
    fi
    
    if [[ -z "$pids" ]]; then
        echo -e "${GREEN}✅ 没有找到匹配 '$pattern' 的项目进程${NC}"
        return 0
    fi
    
    echo -e "${YELLOW}发现匹配的项目进程: $pids${NC}"
    
    for pid in $pids; do
        local cmd=$(ps -p $pid -o args= 2>/dev/null | cut -c1-80 || echo "unknown")
        echo -e "${BLUE}  命令: $cmd${NC}"
        safe_kill_process $pid "$service_name" 10
    done
}

# 主要停止逻辑
main_stop() {
    echo -e "${BLUE}=== 开始停止所有 LLaMA WebServer 相关服务 ===${NC}"
    
    # 1. 停止 HTTP 服务器（端口 8080）
    echo -e "\n${BLUE}--- 停止 HTTP 服务器 ---${NC}"
    stop_service_by_port 8080 "HTTP服务器"
    
    # 按进程名停止
    stop_service_by_name "llama_http_server" "HTTP服务器"
    
    # 2. 停止 LLaMA TCP 服务（端口 8899）
    echo -e "\n${BLUE}--- 停止 LLaMA TCP 服务 ---${NC}"
    stop_service_by_port 8899 "LLaMA TCP服务"
    stop_service_by_name "llama_service_tcp" "LLaMA TCP服务"
    
    # 3. 停止测试和调试进程
    echo -e "\n${BLUE}--- 停止测试和调试进程 ---${NC}"
    stop_service_by_name "test_concurrency" "并发测试"
    stop_service_by_name "performance_tester" "性能测试"
    stop_service_by_name "test_db" "数据库测试"
    
    echo -e "\n${GREEN}=== 服务停止完成 ===${NC}"
}

# 函数：显示当前运行的相关服务
show_running_services() {
    echo -e "${BLUE}=== 当前运行的相关服务 ===${NC}"
    
    local project_dir="/home/shl203/llama-webserver"
    
    echo -e "\n${YELLOW}端口占用情况:${NC}"
    echo "端口 8080 (HTTP服务器):"
    local port_info=$(lsof -i:8080 2>/dev/null || echo "")
    if [[ -n "$port_info" ]]; then
        echo "$port_info"
    else
        echo "  无服务运行"
    fi
    
    echo "端口 8899 (LLaMA TCP服务):"
    port_info=$(lsof -i:8899 2>/dev/null || echo "")
    if [[ -n "$port_info" ]]; then
        echo "$port_info"
    else
        echo "  无服务运行"
    fi
    
    echo -e "\n${YELLOW}项目相关进程:${NC}"
    local found_processes=false
    
    # 查找项目目录下的进程
    local project_processes=$(ps aux | grep "$project_dir" | grep -v grep || true)
    if [[ -n "$project_processes" ]]; then
        echo "项目目录进程:"
        echo "$project_processes"
        found_processes=true
    fi
    
    # 查找特定的项目可执行文件
    local executables=("llama_http_server" "llama_service_tcp")
    for executable in "${executables[@]}"; do
        local exe_processes=$(ps aux | grep "$executable" | grep -v grep || true)
        if [[ -n "$exe_processes" ]]; then
            echo "$executable 进程:"
            echo "$exe_processes"
            found_processes=true
        fi
    done
    
    if [[ "$found_processes" == false ]]; then
        echo "  无项目相关进程运行"
    fi
}

# 函数：显示帮助信息
show_help() {
    cat << EOF
${BLUE}LLaMA WebServer 服务停止脚本${NC}

用法: $0 [选项]

选项:
  -h, --help          显示此帮助信息
  -s, --status        显示当前运行的服务状态
  -p, --port PORT     停止指定端口的服务
  -n, --name PATTERN  停止匹配指定模式的进程

示例:
  $0                  # 正常停止所有服务
  $0 --status         # 查看当前服务状态
  $0 --port 8080      # 停止端口8080上的服务
  $0 --name llama     # 停止包含'llama'的进程

EOF
}

# 解析命令行参数
case "${1:-}" in
    -h|--help)
        show_help
        exit 0
        ;;
    -s|--status)
        show_running_services
        exit 0
        ;;
    -p|--port)
        if [[ -z "$2" ]]; then
            echo -e "${RED}错误: 请指定端口号${NC}"
            exit 1
        fi
        stop_service_by_port "$2" "端口$2上的服务"
        exit 0
        ;;
    -n|--name)
        if [[ -z "$2" ]]; then
            echo -e "${RED}错误: 请指定进程名模式${NC}"
            exit 1
        fi
        stop_service_by_name "$2" "匹配'$2'的进程"
        exit 0
        ;;
    "")
        # 默认行为：正常停止所有服务
        main_stop
        ;;
    *)
        echo -e "${RED}未知选项: $1${NC}"
        show_help
        exit 1
        ;;
esac

# 最终状态检查
echo -e "\n${BLUE}=== 最终状态检查 ===${NC}"
show_running_services

echo -e "\n${GREEN}脚本执行完成${NC}"
