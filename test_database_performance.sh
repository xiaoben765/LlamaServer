#!/bin/bash

# ========================================
# 数据库性能测试脚本
# ========================================

set -e

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# 配置
SERVER_URL="http://127.0.0.1:${SERVER_PORT:-8080}"
DB_FILE="kama.db"
RESULTS_DIR="db_performance_results"
TEST_USERS=100
TEST_SESSIONS=50
TEST_MESSAGES=1000

echo -e "${BLUE}=== 数据库性能测试 ===${NC}"

# 创建结果目录
mkdir -p $RESULTS_DIR

# 检查数据库工具
check_sqlite_tools() {
    if ! command -v sqlite3 &> /dev/null; then
        echo -e "${RED}sqlite3 未安装${NC}"
        echo "安装命令: sudo apt-get install sqlite3"
        exit 1
    fi
}

# 清理测试数据
cleanup_test_data() {
    echo -e "${YELLOW}清理测试数据...${NC}"
    if [ -f "$DB_FILE" ]; then
        sqlite3 "$DB_FILE" "DELETE FROM conversations WHERE user_id LIKE 'test_user_%';"
        sqlite3 "$DB_FILE" "DELETE FROM sessions WHERE user_id LIKE 'test_user_%';"
        sqlite3 "$DB_FILE" "DELETE FROM users WHERE user_id LIKE 'test_user_%';"
        echo "测试数据已清理"
    fi
}

# 准备测试数据
prepare_test_data() {
    echo -e "${YELLOW}准备测试数据...${NC}"
    
    # 创建测试用户
    for i in $(seq 1 $TEST_USERS); do
        curl -s -X POST "$SERVER_URL/api/register" \
            -H "Content-Type: application/json" \
            -d "{\"username\": \"test_user_$i\", \"password\": \"password123\"}" > /dev/null
    done
    
    echo "创建了 $TEST_USERS 个测试用户"
}

# 测试用户注册性能
test_user_registration() {
    echo -e "${BLUE}测试用户注册性能...${NC}"
    
    local start_time=$(date +%s%N)
    
    for i in $(seq 1 100); do
        curl -s -X POST "$SERVER_URL/api/register" \
            -H "Content-Type: application/json" \
            -d "{\"username\": \"perf_user_$i\", \"password\": \"password123\"}" > /dev/null &
        
        if [ $((i % 10)) -eq 0 ]; then
            wait  # 控制并发数
        fi
    done
    wait
    
    local end_time=$(date +%s%N)
    local duration=$((($end_time - $start_time) / 1000000))  # 转换为毫秒
    
    echo "注册 100 个用户耗时: ${duration} ms"
    echo "注册性能: $((100 * 1000 / $duration)) 用户/秒" | tee "$RESULTS_DIR/user_registration_perf.log"
}

# 测试会话创建性能
test_session_creation() {
    echo -e "${BLUE}测试会话创建性能...${NC}"
    
    local start_time=$(date +%s%N)
    
    for i in $(seq 1 $TEST_SESSIONS); do
        local user_id="test_user_$((i % TEST_USERS + 1))"
        curl -s -X POST "$SERVER_URL/api/chat" \
            -H "Content-Type: application/json" \
            -d "{\"user_id\": \"$user_id\", \"message\": \"创建会话测试 $i\"}" > /dev/null &
        
        if [ $((i % 5)) -eq 0 ]; then
            wait
        fi
    done
    wait
    
    local end_time=$(date +%s%N)
    local duration=$((($end_time - $start_time) / 1000000))
    
    echo "创建 $TEST_SESSIONS 个会话耗时: ${duration} ms"
    echo "会话创建性能: $(($TEST_SESSIONS * 1000 / $duration)) 会话/秒" | tee "$RESULTS_DIR/session_creation_perf.log"
}

# 测试消息写入性能
test_message_insertion() {
    echo -e "${BLUE}测试消息写入性能...${NC}"
    
    local start_time=$(date +%s%N)
    
    for i in $(seq 1 $TEST_MESSAGES); do
        local user_id="test_user_$((i % TEST_USERS + 1))"
        curl -s -X POST "$SERVER_URL/api/chat" \
            -H "Content-Type: application/json" \
            -d "{\"user_id\": \"$user_id\", \"message\": \"性能测试消息 $i - $(date)\"}" > /dev/null &
        
        if [ $((i % 20)) -eq 0 ]; then
            wait
        fi
    done
    wait
    
    local end_time=$(date +%s%N)
    local duration=$((($end_time - $start_time) / 1000000))
    
    echo "写入 $TEST_MESSAGES 条消息耗时: ${duration} ms"
    echo "消息写入性能: $(($TEST_MESSAGES * 1000 / $duration)) 消息/秒" | tee "$RESULTS_DIR/message_insertion_perf.log"
}

# 测试查询性能
test_query_performance() {
    echo -e "${BLUE}测试查询性能...${NC}"
    
    # 测试会话列表查询
    echo "测试会话列表查询..."
    local start_time=$(date +%s%N)
    
    for i in $(seq 1 100); do
        local user_id="test_user_$((i % TEST_USERS + 1))"
        curl -s "$SERVER_URL/api/sessions?user_id=$user_id" > /dev/null &
        
        if [ $((i % 10)) -eq 0 ]; then
            wait
        fi
    done
    wait
    
    local end_time=$(date +%s%N)
    local duration=$((($end_time - $start_time) / 1000000))
    echo "会话查询性能: $((100 * 1000 / $duration)) 查询/秒" | tee -a "$RESULTS_DIR/query_performance.log"
    
    # 测试状态查询
    echo "测试状态查询..."
    start_time=$(date +%s%N)
    
    for i in $(seq 1 200); do
        curl -s "$SERVER_URL/api/status" > /dev/null &
        
        if [ $((i % 20)) -eq 0 ]; then
            wait
        fi
    done
    wait
    
    end_time=$(date +%s%N)
    duration=$((($end_time - $start_time) / 1000000))
    echo "状态查询性能: $((200 * 1000 / $duration)) 查询/秒" | tee -a "$RESULTS_DIR/query_performance.log"
}

# 测试缓存性能
test_cache_performance() {
    echo -e "${BLUE}测试缓存性能...${NC}"
    
    local test_message="缓存测试消息 - 这是一个用于测试缓存性能的消息"
    
    # 第一次请求 (缓存未命中)
    echo "测试缓存未命中性能..."
    local start_time=$(date +%s%N)
    curl -s -X POST "$SERVER_URL/api/chat" \
        -H "Content-Type: application/json" \
        -d "{\"message\": \"$test_message\"}" > /dev/null
    local end_time=$(date +%s%N)
    local uncached_time=$((($end_time - $start_time) / 1000000))
    
    # 后续请求 (缓存命中)
    echo "测试缓存命中性能..."
    local total_cached_time=0
    local cached_requests=10
    
    for i in $(seq 1 $cached_requests); do
        start_time=$(date +%s%N)
        curl -s -X POST "$SERVER_URL/api/chat" \
            -H "Content-Type: application/json" \
            -d "{\"message\": \"$test_message\"}" > /dev/null
        end_time=$(date +%s%N)
        local cached_time=$((($end_time - $start_time) / 1000000))
        total_cached_time=$((total_cached_time + cached_time))
    done
    
    local avg_cached_time=$((total_cached_time / cached_requests))
    local speedup=$((uncached_time / avg_cached_time))
    
    echo "缓存未命中时间: ${uncached_time} ms" | tee "$RESULTS_DIR/cache_performance.log"
    echo "缓存命中平均时间: ${avg_cached_time} ms" | tee -a "$RESULTS_DIR/cache_performance.log"
    echo "缓存加速比: ${speedup}x" | tee -a "$RESULTS_DIR/cache_performance.log"
}

# 分析数据库统计信息
analyze_database_stats() {
    echo -e "${BLUE}分析数据库统计信息...${NC}"
    
    if [ ! -f "$DB_FILE" ]; then
        echo "数据库文件不存在"
        return
    fi
    
    # 数据库大小
    local db_size=$(du -h "$DB_FILE" | cut -f1)
    echo "数据库文件大小: $db_size"
    
    # 表统计信息
    echo "表记录统计:" | tee "$RESULTS_DIR/database_stats.log"
    sqlite3 "$DB_FILE" "SELECT 'users: ' || COUNT(*) FROM users;" | tee -a "$RESULTS_DIR/database_stats.log"
    sqlite3 "$DB_FILE" "SELECT 'sessions: ' || COUNT(*) FROM sessions;" | tee -a "$RESULTS_DIR/database_stats.log"
    sqlite3 "$DB_FILE" "SELECT 'conversations: ' || COUNT(*) FROM conversations;" | tee -a "$RESULTS_DIR/database_stats.log"
    sqlite3 "$DB_FILE" "SELECT 'cache_entries: ' || COUNT(*) FROM cache_entries;" | tee -a "$RESULTS_DIR/database_stats.log"
    
    # 索引信息
    echo "索引信息:" | tee -a "$RESULTS_DIR/database_stats.log"
    sqlite3 "$DB_FILE" ".schema" | grep -i "index" | tee -a "$RESULTS_DIR/database_stats.log"
    
    # 查询计划分析
    echo "查询计划分析:" | tee -a "$RESULTS_DIR/database_stats.log"
    sqlite3 "$DB_FILE" "EXPLAIN QUERY PLAN SELECT * FROM conversations WHERE session_id = 'test';" | tee -a "$RESULTS_DIR/database_stats.log"
}

# 生成数据库性能报告
generate_db_report() {
    echo -e "${BLUE}生成数据库性能报告...${NC}"
    
    local report_file="$RESULTS_DIR/database_performance_report.md"
    
    cat > $report_file << EOF
# 数据库性能测试报告

生成时间: $(date)
数据库文件: $DB_FILE
测试规模: $TEST_USERS 用户, $TEST_SESSIONS 会话, $TEST_MESSAGES 消息

## 测试结果

### 用户注册性能
EOF

    if [ -f "$RESULTS_DIR/user_registration_perf.log" ]; then
        echo '```' >> $report_file
        cat "$RESULTS_DIR/user_registration_perf.log" >> $report_file
        echo '```' >> $report_file
    fi

    cat >> $report_file << EOF

### 会话创建性能
EOF

    if [ -f "$RESULTS_DIR/session_creation_perf.log" ]; then
        echo '```' >> $report_file
        cat "$RESULTS_DIR/session_creation_perf.log" >> $report_file
        echo '```' >> $report_file
    fi

    cat >> $report_file << EOF

### 消息写入性能
EOF

    if [ -f "$RESULTS_DIR/message_insertion_perf.log" ]; then
        echo '```' >> $report_file
        cat "$RESULTS_DIR/message_insertion_perf.log" >> $report_file
        echo '```' >> $report_file
    fi

    cat >> $report_file << EOF

### 查询性能
EOF

    if [ -f "$RESULTS_DIR/query_performance.log" ]; then
        echo '```' >> $report_file
        cat "$RESULTS_DIR/query_performance.log" >> $report_file
        echo '```' >> $report_file
    fi

    cat >> $report_file << EOF

### 缓存性能
EOF

    if [ -f "$RESULTS_DIR/cache_performance.log" ]; then
        echo '```' >> $report_file
        cat "$RESULTS_DIR/cache_performance.log" >> $report_file
        echo '```' >> $report_file
    fi

    cat >> $report_file << EOF

### 数据库统计信息
EOF

    if [ -f "$RESULTS_DIR/database_stats.log" ]; then
        echo '```' >> $report_file
        cat "$RESULTS_DIR/database_stats.log" >> $report_file
        echo '```' >> $report_file
    fi

    cat >> $report_file << EOF

## 优化建议

1. **索引优化**: 为高频查询字段添加索引
2. **查询优化**: 使用 EXPLAIN QUERY PLAN 分析慢查询
3. **缓存策略**: 增加热点数据的缓存时间
4. **连接池**: 使用数据库连接池减少连接开销
5. **批量操作**: 对于大量插入操作使用事务批处理

## 性能基准

- 用户注册: > 100 用户/秒
- 消息写入: > 500 消息/秒  
- 查询响应: < 10 ms
- 缓存命中: > 10x 加速
EOF

    echo -e "${GREEN}数据库性能报告已生成: $report_file${NC}"
}

# 主函数
main() {
    echo "开始数据库性能测试..."
    
    check_sqlite_tools
    cleanup_test_data
    prepare_test_data
    
    test_user_registration
    test_session_creation
    test_message_insertion
    test_query_performance
    test_cache_performance
    
    analyze_database_stats
    generate_db_report
    
    cleanup_test_data
    
    echo -e "${GREEN}数据库性能测试完成！${NC}"
    echo "结果目录: $RESULTS_DIR"
    echo "测试报告: $RESULTS_DIR/database_performance_report.md"
}

# 运行主程序
main "$@"
