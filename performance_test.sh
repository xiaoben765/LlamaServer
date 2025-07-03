#!/bin/bash

# ========================================
# Kama-WebServer 综合性能测试套件
# ========================================

set -e

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 测试配置
SERVER_HOST="127.0.0.1"
SERVER_PORT="${SERVER_PORT:-8080}"  # 使用环境变量或默认8080
LLAMA_SERVICE_PORT="8899"
TEST_DURATION=60
CONCURRENT_USERS=(1 5 10 20 50 100)
WARMUP_TIME=10

# 日志文件
PERFORMANCE_LOG="performance_test_$(date +%Y%m%d_%H%M%S).log"
RESULTS_DIR="performance_results"

echo -e "${BLUE}=== Kama-WebServer 性能测试套件 ===${NC}"
echo "开始时间: $(date)"
echo "日志文件: $PERFORMANCE_LOG"

# 创建结果目录
mkdir -p $RESULTS_DIR

# 检查依赖工具
check_dependencies() {
    echo -e "${YELLOW}检查测试依赖...${NC}"
    
    local deps=("curl" "ab" "wrk" "htop" "sar" "iostat")
    local missing=()
    
    for dep in "${deps[@]}"; do
        if ! command -v $dep &> /dev/null; then
            missing+=($dep)
        fi
    done
    
    if [ ${#missing[@]} -ne 0 ]; then
        echo -e "${RED}缺少以下工具: ${missing[*]}${NC}"
        echo "安装命令:"
        echo "sudo apt-get install apache2-utils wrk sysstat htop"
        exit 1
    fi
    
    echo -e "${GREEN}所有依赖工具已安装${NC}"
}

# 启动性能监控
start_monitoring() {
    echo -e "${YELLOW}启动系统监控...${NC}"
    
    # CPU 和内存监控
    sar -u -r 1 > "$RESULTS_DIR/system_stats.log" &
    SAR_PID=$!
    
    # I/O 监控
    iostat -x 1 > "$RESULTS_DIR/io_stats.log" &
    IOSTAT_PID=$!
    
    echo "监控进程启动: SAR($SAR_PID), IOSTAT($IOSTAT_PID)"
}

# 停止监控
stop_monitoring() {
    echo -e "${YELLOW}停止系统监控...${NC}"
    if [ ! -z "$SAR_PID" ]; then
        kill $SAR_PID 2>/dev/null || true
    fi
    if [ ! -z "$IOSTAT_PID" ]; then
        kill $IOSTAT_PID 2>/dev/null || true
    fi
}

# 检查服务器状态
check_server() {
    echo -e "${YELLOW}检查服务器状态...${NC}"
    
    # 检查 HTTP 服务
    if curl -s "http://$SERVER_HOST:$SERVER_PORT/api/status" > /dev/null; then
        echo -e "${GREEN}HTTP 服务器运行正常${NC}"
    else
        echo -e "${RED}HTTP 服务器未响应，请先启动服务器${NC}"
        exit 1
    fi
    
    # 检查 LLaMA 服务 (可选)
    if nc -z $SERVER_HOST $LLAMA_SERVICE_PORT 2>/dev/null; then
        echo -e "${GREEN}LLaMA 服务运行正常${NC}"
    else
        echo -e "${YELLOW}LLaMA 服务未运行，将使用模拟服务${NC}"
    fi
}

# 预热测试
warmup_server() {
    echo -e "${YELLOW}服务器预热 ($WARMUP_TIME 秒)...${NC}"
    
    curl -s -X POST "http://$SERVER_HOST:$SERVER_PORT/api/chat" \
        -H "Content-Type: application/json" \
        -d '{"message": "预热测试"}' > /dev/null
    
    ab -n 10 -c 2 "http://$SERVER_HOST:$SERVER_PORT/api/status" > /dev/null 2>&1
    
    sleep $WARMUP_TIME
    echo -e "${GREEN}预热完成${NC}"
}

# HTTP 性能测试 - Apache Bench
test_http_ab() {
    local concurrent=$1
    local requests=$((concurrent * 100))
    
    echo -e "${BLUE}Apache Bench 测试 - 并发用户: $concurrent${NC}"
    
    # 状态接口测试
    ab -n $requests -c $concurrent -g "$RESULTS_DIR/ab_status_c${concurrent}.tsv" \
        "http://$SERVER_HOST:$SERVER_PORT/api/status" \
        > "$RESULTS_DIR/ab_status_c${concurrent}.log" 2>&1
    
    # 聊天接口测试 (POST)
    echo '{"message": "性能测试消息"}' > /tmp/post_data.json
    ab -n $requests -c $concurrent -p /tmp/post_data.json -T "application/json" \
        -g "$RESULTS_DIR/ab_chat_c${concurrent}.tsv" \
        "http://$SERVER_HOST:$SERVER_PORT/api/chat" \
        > "$RESULTS_DIR/ab_chat_c${concurrent}.log" 2>&1
    
    # 提取关键指标
    local rps=$(grep "Requests per second" "$RESULTS_DIR/ab_status_c${concurrent}.log" | awk '{print $4}')
    local mean_time=$(grep "Time per request" "$RESULTS_DIR/ab_status_c${concurrent}.log" | head -1 | awk '{print $4}')
    local failed=$(grep "Failed requests" "$RESULTS_DIR/ab_status_c${concurrent}.log" | awk '{print $3}')
    
    echo "并发:$concurrent, RPS:$rps, 平均响应时间:${mean_time}ms, 失败:$failed" >> "$RESULTS_DIR/ab_summary.log"
}

# HTTP 性能测试 - wrk
test_http_wrk() {
    local concurrent=$1
    local threads=$((concurrent > 10 ? 10 : concurrent))
    
    echo -e "${BLUE}wrk 测试 - 并发连接: $concurrent${NC}"
    
    # GET 请求测试
    wrk -t$threads -c$concurrent -d${TEST_DURATION}s \
        --latency "http://$SERVER_HOST:$SERVER_PORT/api/status" \
        > "$RESULTS_DIR/wrk_get_c${concurrent}.log" 2>&1
    
    # POST 请求测试
    wrk -t$threads -c$concurrent -d${TEST_DURATION}s \
        --latency -s performance_test_post.lua \
        "http://$SERVER_HOST:$SERVER_PORT/api/chat" \
        > "$RESULTS_DIR/wrk_post_c${concurrent}.log" 2>&1 || true
    
    # 提取关键指标
    local rps=$(grep "Requests/sec" "$RESULTS_DIR/wrk_get_c${concurrent}.log" | awk '{print $2}')
    local latency_avg=$(grep "Latency" "$RESULTS_DIR/wrk_get_c${concurrent}.log" | awk '{print $2}')
    local latency_99=$(grep "99%" "$RESULTS_DIR/wrk_get_c${concurrent}.log" | awk '{print $2}')
    
    echo "并发:$concurrent, RPS:$rps, 平均延迟:$latency_avg, 99%延迟:$latency_99" >> "$RESULTS_DIR/wrk_summary.log"
}

# 并发模型性能测试
test_concurrency_model() {
    echo -e "${BLUE}测试并发模型性能...${NC}"
    
    # 构建并运行并发测试程序
    if [ -f "./bin/test_concurrency" ]; then
        echo "运行异步任务队列测试..."
        timeout 30s ./bin/test_concurrency --async > "$RESULTS_DIR/async_queue_test.log" 2>&1 || true
        
        echo "运行模型实例池测试..."
        timeout 30s ./bin/test_concurrency --pool > "$RESULTS_DIR/instance_pool_test.log" 2>&1 || true
        
        echo "运行异步服务测试..."
        timeout 30s ./bin/test_concurrency --service > "$RESULTS_DIR/async_service_test.log" 2>&1 || true
    else
        echo -e "${YELLOW}test_concurrency 未找到，跳过内部并发测试${NC}"
    fi
}

# 内存泄漏测试
test_memory_leak() {
    echo -e "${BLUE}内存泄漏检测...${NC}"
    
    # 获取初始内存使用
    local initial_mem=$(ps -o rss= -p $(pgrep -f "main_http") 2>/dev/null | awk '{sum+=$1} END {print sum}')
    
    if [ -z "$initial_mem" ]; then
        echo -e "${YELLOW}无法检测服务器进程，跳过内存测试${NC}"
        return
    fi
    
    echo "初始内存使用: ${initial_mem}KB"
    
    # 运行持续负载测试
    echo "运行持续负载测试 (5分钟)..."
    wrk -t4 -c20 -d300s "http://$SERVER_HOST:$SERVER_PORT/api/status" > /dev/null 2>&1 &
    WRK_PID=$!
    
    # 每30秒记录一次内存使用
    for i in {1..10}; do
        sleep 30
        local current_mem=$(ps -o rss= -p $(pgrep -f "main_http") 2>/dev/null | awk '{sum+=$1} END {print sum}')
        echo "$(date) - 内存使用: ${current_mem}KB" >> "$RESULTS_DIR/memory_usage.log"
    done
    
    kill $WRK_PID 2>/dev/null || true
    
    local final_mem=$(ps -o rss= -p $(pgrep -f "main_http") 2>/dev/null | awk '{sum+=$1} END {print sum}')
    echo "最终内存使用: ${final_mem}KB"
    
    if [ ! -z "$final_mem" ] && [ ! -z "$initial_mem" ]; then
        local mem_increase=$((final_mem - initial_mem))
        echo "内存增长: ${mem_increase}KB" >> "$RESULTS_DIR/memory_usage.log"
        
        if [ $mem_increase -gt 10000 ]; then
            echo -e "${YELLOW}警告: 内存增长较大 (${mem_increase}KB)${NC}"
        else
            echo -e "${GREEN}内存使用稳定${NC}"
        fi
    fi
}

# 数据库性能测试
test_database_performance() {
    echo -e "${BLUE}数据库性能测试...${NC}"
    
    # 大量聊天请求测试数据库写入性能
    echo "测试数据库写入性能..."
    
    for i in {1..100}; do
        curl -s -X POST "http://$SERVER_HOST:$SERVER_PORT/api/chat" \
            -H "Content-Type: application/json" \
            -d "{\"message\": \"数据库测试消息 $i\"}" > /dev/null &
        
        if [ $((i % 10)) -eq 0 ]; then
            wait  # 等待每批完成
        fi
    done
    wait
    
    # 测试会话查询性能
    echo "测试数据库查询性能..."
    ab -n 100 -c 10 "http://$SERVER_HOST:$SERVER_PORT/api/sessions" \
        > "$RESULTS_DIR/db_query_test.log" 2>&1
}

# 创建 wrk POST 测试脚本
create_wrk_post_script() {
    cat > performance_test_post.lua << 'EOF'
wrk.method = "POST"
wrk.body   = '{"message": "wrk性能测试消息"}'
wrk.headers["Content-Type"] = "application/json"
EOF
}

# 生成性能报告
generate_report() {
    echo -e "${BLUE}生成性能测试报告...${NC}"
    
    local report_file="$RESULTS_DIR/performance_report.md"
    
    cat > $report_file << EOF
# Kama-WebServer 性能测试报告

生成时间: $(date)
测试持续时间: ${TEST_DURATION}秒
测试并发级别: ${CONCURRENT_USERS[*]}

## 测试环境

- 操作系统: $(uname -a)
- CPU: $(lscpu | grep "Model name" | cut -d: -f2 | xargs)
- 内存: $(free -h | grep Mem | awk '{print $2}')
- 服务器: $SERVER_HOST:$SERVER_PORT

## Apache Bench 测试结果

### 状态接口 (GET /api/status)
EOF

    if [ -f "$RESULTS_DIR/ab_summary.log" ]; then
        echo '```' >> $report_file
        cat "$RESULTS_DIR/ab_summary.log" >> $report_file
        echo '```' >> $report_file
    fi

    cat >> $report_file << EOF

## wrk 测试结果

### GET 请求性能
EOF

    if [ -f "$RESULTS_DIR/wrk_summary.log" ]; then
        echo '```' >> $report_file
        cat "$RESULTS_DIR/wrk_summary.log" >> $report_file
        echo '```' >> $report_file
    fi

    # 添加系统资源使用情况
    cat >> $report_file << EOF

## 系统资源使用

### CPU 使用率
EOF

    if [ -f "$RESULTS_DIR/system_stats.log" ]; then
        echo "平均 CPU 使用率:" >> $report_file
        echo '```' >> $report_file
        tail -100 "$RESULTS_DIR/system_stats.log" | grep -v "Average" | awk 'NF>3 {print "用户:", $3"%, 系统:", $5"%, 空闲:", $8"%"}' | tail -10 >> $report_file
        echo '```' >> $report_file
    fi

    cat >> $report_file << EOF

### 内存使用
EOF

    if [ -f "$RESULTS_DIR/memory_usage.log" ]; then
        echo '```' >> $report_file
        cat "$RESULTS_DIR/memory_usage.log" >> $report_file
        echo '```' >> $report_file
    fi

    cat >> $report_file << EOF

## 建议

1. **高并发优化**: 如果 QPS 低于预期，考虑增加线程池大小
2. **内存优化**: 监控内存使用，确保无内存泄漏
3. **数据库优化**: 对于高频查询，考虑增加缓存
4. **网络优化**: 使用连接池和 Keep-Alive

## 详细日志

- Apache Bench 详细结果: $RESULTS_DIR/ab_*.log
- wrk 详细结果: $RESULTS_DIR/wrk_*.log
- 系统监控日志: $RESULTS_DIR/system_stats.log
- I/O 监控日志: $RESULTS_DIR/io_stats.log
EOF

    echo -e "${GREEN}性能报告已生成: $report_file${NC}"
}

# 清理函数
cleanup() {
    echo -e "${YELLOW}清理测试环境...${NC}"
    stop_monitoring
    rm -f /tmp/post_data.json performance_test_post.lua
    kill $(jobs -p) 2>/dev/null || true
}

# 主测试流程
main() {
    # 设置清理陷阱
    trap cleanup EXIT
    
    echo "开始性能测试..." | tee -a $PERFORMANCE_LOG
    
    # 检查依赖
    check_dependencies
    
    # 启动监控
    start_monitoring
    
    # 检查服务器
    check_server
    
    # 预热
    warmup_server
    
    # 创建 wrk POST 脚本
    create_wrk_post_script
    
    # 并发测试
    echo -e "${BLUE}开始并发性能测试...${NC}"
    for concurrent in "${CONCURRENT_USERS[@]}"; do
        echo -e "${YELLOW}测试并发用户数: $concurrent${NC}"
        
        # Apache Bench 测试
        test_http_ab $concurrent
        sleep 5
        
        # wrk 测试
        test_http_wrk $concurrent
        sleep 10
    done
    
    # 专项测试
    test_concurrency_model
    test_database_performance
    test_memory_leak
    
    # 生成报告
    generate_report
    
    echo -e "${GREEN}性能测试完成！${NC}"
    echo "结果目录: $RESULTS_DIR"
    echo "测试报告: $RESULTS_DIR/performance_report.md"
}

# 处理命令行参数
case "${1:-}" in
    --help|-h)
        echo "用法: $0 [选项]"
        echo "选项:"
        echo "  --quick    快速测试 (较少并发用户)"
        echo "  --stress   压力测试 (更多并发用户)"
        echo "  --help     显示此帮助"
        exit 0
        ;;
    --quick)
        CONCURRENT_USERS=(1 5 10)
        TEST_DURATION=30
        echo "快速测试模式"
        ;;
    --stress)
        CONCURRENT_USERS=(1 10 20 50 100 200 500)
        TEST_DURATION=120
        echo "压力测试模式"
        ;;
esac

# 运行主程序
main "$@"
