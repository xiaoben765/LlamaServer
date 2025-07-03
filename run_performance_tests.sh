#!/bin/bash

# ========================================
# Kama-WebServer 一键性能测试脚本
# ========================================

set -e

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

# 显示帮助信息
show_help() {
    cat << EOF
🚀 Kama-WebServer 一键性能测试工具

用法: $0 [选项]

选项:
  -q, --quick       快速测试 (3-5分钟)
  -f, --full        完整测试 (15-20分钟)  
  -s, --stress      压力测试 (30-60分钟)
  -d, --database    数据库性能测试
  -c, --custom      自定义测试工具
  -a, --all         运行所有测试
  -h, --help        显示此帮助信息

示例:
  $0 --quick        # 快速性能测试
  $0 --full         # 完整性能测试
  $0 --all          # 运行全部测试

EOF
}

# 检查服务器状态
check_server_status() {
    echo -e "${CYAN}检查服务器状态...${NC}"
    
    local server_running=false
    local server_port=""
    
    # 检查常见端口的 HTTP 服务器
    for port in 8080 8081 8082 3000; do
        if curl -s --connect-timeout 3 "http://127.0.0.1:$port/api/status" > /dev/null 2>&1; then
            echo -e "${GREEN}✓ HTTP 服务器运行正常 (端口 $port)${NC}"
            server_running=true
            server_port=$port
            export SERVER_PORT=$port  # 导出给其他脚本使用
            break
        fi
    done
    
    if [ "$server_running" = false ]; then
        echo -e "${YELLOW}⚠ HTTP 服务器未在常见端口 (8080, 8081, 8082, 3000) 运行${NC}"
    fi
    
    # 检查 LLaMA 服务
    if nc -z 127.0.0.1 8899 2>/dev/null; then
        echo -e "${GREEN}✓ LLaMA 服务运行正常 (端口 8899)${NC}"
    else
        echo -e "${YELLOW}⚠ LLaMA 服务未运行 (端口 8899) - 将使用模拟服务${NC}"
    fi
    
    if [ "$server_running" = false ]; then
        echo -e "${RED}❌ 主服务器未运行，请先启动服务器:${NC}"
        echo "   ./bin/main_http"
        echo "   或"
        echo "   ./start_services.sh"
        exit 1
    fi
}

# 检查测试工具
check_test_tools() {
    echo -e "${CYAN}检查测试工具...${NC}"
    
    local missing_tools=()
    
    # 检查基本工具
    local tools=("curl" "ab" "wrk" "sqlite3")
    for tool in "${tools[@]}"; do
        if ! command -v $tool &> /dev/null; then
            missing_tools+=($tool)
        fi
    done
    
    if [ ${#missing_tools[@]} -ne 0 ]; then
        echo -e "${RED}❌ 缺少以下工具: ${missing_tools[*]}${NC}"
        echo "安装命令:"
        echo "  sudo apt-get install apache2-utils wrk sqlite3"
        echo "  或运行: make -f Makefile.performance install-deps"
        exit 1
    fi
    
    echo -e "${GREEN}✓ 所有测试工具已安装${NC}"
}

# 构建自定义测试工具
build_custom_tester() {
    echo -e "${CYAN}构建自定义测试工具...${NC}"
    
    if [ ! -f "bin/performance_tester" ]; then
        if [ -f "src/performance_tester.cpp" ]; then
            echo "编译性能测试工具..."
            make -f Makefile.performance bin/performance_tester 2>/dev/null || {
                echo -e "${YELLOW}⚠ 无法构建自定义测试工具，将跳过相关测试${NC}"
                return 1
            }
            echo -e "${GREEN}✓ 自定义测试工具构建完成${NC}"
        else
            echo -e "${YELLOW}⚠ 自定义测试工具源码不存在${NC}"
            return 1
        fi
    else
        echo -e "${GREEN}✓ 自定义测试工具已存在${NC}"
    fi
    return 0
}

# 显示测试菜单
show_test_menu() {
    echo -e "${BLUE}"
    echo "=================================================="
    echo "      🚀 Kama-WebServer 性能测试套件"
    echo "=================================================="
    echo -e "${NC}"
    echo "请选择测试类型:"
    echo ""
    echo "  1) 快速测试     - 基础性能验证 (3-5分钟)"
    echo "  2) 完整测试     - 全面性能评估 (15-20分钟)"
    echo "  3) 压力测试     - 极限性能测试 (30-60分钟)"
    echo "  4) 数据库测试   - 数据库性能专项测试"
    echo "  5) 自定义测试   - C++ 自定义测试工具"
    echo "  6) 全部测试     - 运行所有测试项目"
    echo "  0) 退出"
    echo ""
    echo -n "请输入选择 [0-6]: "
}

# 运行快速测试
run_quick_test() {
    echo -e "${BLUE}=== 快速性能测试 ===${NC}"
    echo "测试时长: 3-5分钟"
    echo "测试内容: 基础 HTTP 性能，轻量级并发测试"
    echo ""
    
    ./performance_test.sh --quick
}

# 运行完整测试
run_full_test() {
    echo -e "${BLUE}=== 完整性能测试 ===${NC}"
    echo "测试时长: 15-20分钟"
    echo "测试内容: HTTP 性能，并发测试，系统资源监控"
    echo ""
    
    ./performance_test.sh
}

# 运行压力测试
run_stress_test() {
    echo -e "${BLUE}=== 压力测试 ===${NC}"
    echo "测试时长: 30-60分钟"
    echo "测试内容: 高并发压力测试，长期稳定性测试"
    echo ""
    echo -e "${YELLOW}注意: 压力测试会对系统产生较高负载${NC}"
    echo -n "确认继续? [y/N]: "
    read -r confirm
    if [[ $confirm =~ ^[Yy]$ ]]; then
        ./performance_test.sh --stress
    else
        echo "已取消压力测试"
    fi
}

# 运行数据库测试
run_database_test() {
    echo -e "${BLUE}=== 数据库性能测试 ===${NC}"
    echo "测试时长: 10-15分钟"
    echo "测试内容: 数据库读写性能，缓存效果，查询优化"
    echo ""
    
    ./test_database_performance.sh
}

# 运行自定义测试
run_custom_test() {
    echo -e "${BLUE}=== 自定义测试工具 ===${NC}"
    echo "测试时长: 5-10分钟"
    echo "测试内容: C++ 编写的专用性能测试"
    echo ""
    
    if build_custom_tester; then
        ./bin/performance_tester
    else
        echo -e "${RED}无法运行自定义测试工具${NC}"
    fi
}

# 运行全部测试
run_all_tests() {
    echo -e "${BLUE}=== 运行全部测试 ===${NC}"
    echo "预计总时长: 60-90分钟"
    echo ""
    echo -e "${YELLOW}这将运行所有性能测试，需要较长时间${NC}"
    echo -n "确认继续? [y/N]: "
    read -r confirm
    if [[ $confirm =~ ^[Yy]$ ]]; then
        echo ""
        echo -e "${CYAN}开始全面性能测试...${NC}"
        
        run_quick_test
        echo -e "\n${GREEN}快速测试完成${NC}\n"
        sleep 3
        
        run_database_test
        echo -e "\n${GREEN}数据库测试完成${NC}\n"
        sleep 3
        
        if build_custom_tester; then
            run_custom_test
            echo -e "\n${GREEN}自定义测试完成${NC}\n"
            sleep 3
        fi
        
        run_full_test
        echo -e "\n${GREEN}完整测试完成${NC}\n"
        sleep 3
        
        echo -e "${CYAN}是否运行压力测试? [y/N]: ${NC}"
        read -r stress_confirm
        if [[ $stress_confirm =~ ^[Yy]$ ]]; then
            run_stress_test
        fi
        
        echo -e "\n${GREEN}🎉 全部测试完成！${NC}"
    else
        echo "已取消全部测试"
    fi
}

# 显示测试结果
show_results() {
    echo -e "${BLUE}=== 测试结果 ===${NC}"
    echo ""
    
    # 检查是否有结果文件
    if [ -d "performance_results" ] && [ "$(ls -A performance_results 2>/dev/null)" ]; then
        echo -e "${GREEN}✓ HTTP 性能测试结果:${NC}"
        echo "  📄 详细报告: performance_results/performance_report.md"
        
        if [ -f "performance_results/performance_report.md" ]; then
            echo "  📊 关键指标:"
            grep -E "(QPS|响应时间|并发)" performance_results/performance_report.md 2>/dev/null | head -5 | sed 's/^/    /'
        fi
    fi
    
    if [ -d "db_performance_results" ] && [ "$(ls -A db_performance_results 2>/dev/null)" ]; then
        echo -e "\n${GREEN}✓ 数据库性能测试结果:${NC}"
        echo "  📄 详细报告: db_performance_results/database_performance_report.md"
    fi
    
    echo ""
    echo -e "${CYAN}💡 查看完整结果:${NC}"
    echo "  cat performance_results/performance_report.md"
    echo "  cat db_performance_results/database_performance_report.md"
}

# 清理测试结果
clean_results() {
    echo -e "${YELLOW}清理之前的测试结果...${NC}"
    echo -n "确认删除之前的测试结果? [y/N]: "
    read -r confirm
    if [[ $confirm =~ ^[Yy]$ ]]; then
        rm -rf performance_results/ db_performance_results/ 2>/dev/null || true
        echo -e "${GREEN}✓ 测试结果已清理${NC}"
    fi
}

# 主函数
main() {
    # 检查命令行参数
    case "${1:-}" in
        -h|--help)
            show_help
            exit 0
            ;;
        -q|--quick)
            check_server_status
            check_test_tools
            run_quick_test
            show_results
            exit 0
            ;;
        -f|--full)
            check_server_status
            check_test_tools
            run_full_test
            show_results
            exit 0
            ;;
        -s|--stress)
            check_server_status
            check_test_tools
            run_stress_test
            show_results
            exit 0
            ;;
        -d|--database)
            check_server_status
            check_test_tools
            run_database_test
            show_results
            exit 0
            ;;
        -c|--custom)
            check_server_status
            check_test_tools
            run_custom_test
            exit 0
            ;;
        -a|--all)
            check_server_status
            check_test_tools
            run_all_tests
            show_results
            exit 0
            ;;
        --clean)
            clean_results
            exit 0
            ;;
        "")
            # 交互模式
            ;;
        *)
            echo -e "${RED}未知选项: $1${NC}"
            show_help
            exit 1
            ;;
    esac
    
    # 交互模式
    check_server_status
    check_test_tools
    
    while true; do
        echo ""
        show_test_menu
        read -r choice
        
        case $choice in
            1)
                run_quick_test
                show_results
                ;;
            2)
                run_full_test
                show_results
                ;;
            3)
                run_stress_test
                show_results
                ;;
            4)
                run_database_test
                show_results
                ;;
            5)
                run_custom_test
                ;;
            6)
                run_all_tests
                show_results
                ;;
            0)
                echo -e "${GREEN}感谢使用 Kama-WebServer 性能测试工具！${NC}"
                exit 0
                ;;
            *)
                echo -e "${RED}无效选择，请输入 0-6${NC}"
                ;;
        esac
        
        echo ""
        echo -n "按 Enter 键继续..."
        read -r
    done
}

# 运行主程序
main "$@"
