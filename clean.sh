#!/bin/bash

# 项目清理脚本
# 用于清理构建产物、日志文件和临时文件

set -e
set -o pipefail

# 显示彩色输出
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# 确保在正确的目录中
cd "$(dirname "$0")"
PROJECT_DIR=$(pwd)

echo -e "${BLUE}=== LLaMA WebServer 项目清理工具 ===${NC}"
echo -e "清理时间: ${CYAN}$(date '+%Y-%m-%d %H:%M:%S')${NC}"

# 函数：显示帮助信息
show_help() {
    cat << EOF
${BLUE}用法: $0 [选项]${NC}

${YELLOW}选项:${NC}
    ${GREEN}--build${NC}        清理构建缓存文件 (build/, lib/, CMakeFiles/)
    ${GREEN}--logs${NC}         清理日志文件 (logs/*)
    ${GREEN}--temp${NC}         清理临时文件 (*.tmp, *.bak, *~)
    ${GREEN}--help, -h${NC}     显示此帮助信息

${YELLOW}示例:${NC}
    $0 --build           # 清理构建文件
    $0 --logs            # 清理日志文件
    $0 --temp            # 清理临时文件
    $0 --build --logs    # 清理构建文件和日志

EOF
}

# 函数：计算目录大小
get_dir_size() {
    local dir="$1"
    if [ -d "$dir" ]; then
        du -sh "$dir" 2>/dev/null | cut -f1
    else
        echo "0"
    fi
}

# 函数：清理构建文件
clean_build() {
    echo -e "\n${BLUE}--- 清理构建文件 ---${NC}"
    
    local build_size=$(get_dir_size "build")
    local lib_size=$(get_dir_size "lib") 
    local cmake_size=$(get_dir_size "CMakeFiles")
    
    # 删除构建目录
    if [ -d "build" ]; then
        echo -e "${YELLOW}正在删除构建目录 (大小: $build_size)...${NC}"
        rm -rf build/
        echo -e "${GREEN}✅ 已删除 build/ 目录${NC}"
    else
        echo -e "${CYAN}build/ 目录不存在，跳过${NC}"
    fi
    
    # 删除库文件目录
    if [ -d "lib" ]; then
        echo -e "${YELLOW}正在删除库文件目录 (大小: $lib_size)...${NC}"
        rm -rf lib/
        echo -e "${GREEN}✅ 已删除 lib/ 目录${NC}"
    else
        echo -e "${CYAN}lib/ 目录不存在，跳过${NC}"
    fi
    
    # 删除CMake缓存目录
    if [ -d "CMakeFiles" ]; then
        echo -e "${YELLOW}正在删除CMake缓存目录 (大小: $cmake_size)...${NC}"
        rm -rf CMakeFiles/
        echo -e "${GREEN}✅ 已删除 CMakeFiles/ 目录${NC}"
    else
        echo -e "${CYAN}CMakeFiles/ 目录不存在，跳过${NC}"
    fi
    
    # 清理编译产物 (保留bin目录下的文件)
    local found_objects=false
    find . -name "*.o" -o -name "*.a" -o -name "*.so" 2>/dev/null | while read file; do
        if [[ "$file" != "./bin/"* ]]; then
            found_objects=true
            echo -e "${YELLOW}删除编译产物: $file${NC}"
            rm -f "$file"
        fi
    done
    
    echo -e "${GREEN}✅ 构建文件清理完成${NC}"
    echo -e "释放空间: ${CYAN}$build_size + $lib_size + $cmake_size${NC}"
}

# 函数：清理日志文件
clean_logs() {
    echo -e "\n${BLUE}--- 清理日志文件 ---${NC}"
    
    if [ -d "logs" ]; then
        local logs_size=$(get_dir_size "logs")
        local file_count=$(find logs/ -type f 2>/dev/null | wc -l)
        
        if [ "$file_count" -gt 0 ]; then
            echo -e "${YELLOW}正在清理 $file_count 个日志文件 (大小: $logs_size)...${NC}"
            find logs/ -type f -delete 2>/dev/null
            echo -e "${GREEN}✅ 日志文件清理完成${NC}"
            echo -e "释放空间: ${CYAN}$logs_size${NC}"
        else
            echo -e "${CYAN}logs/ 目录为空，无需清理${NC}"
        fi
    else
        echo -e "${CYAN}logs/ 目录不存在，跳过${NC}"
    fi
}

# 函数：清理临时文件
clean_temp() {
    echo -e "\n${BLUE}--- 清理临时文件 ---${NC}"
    
    local temp_patterns=(
        "*.tmp"
        "*.bak"
        "*~"
        "*.swp"
        "*.swo"
    )
    
    local found_files=false
    for pattern in "${temp_patterns[@]}"; do
        while IFS= read -r -d '' file; do
            found_files=true
            echo -e "${YELLOW}删除临时文件: $file${NC}"
            rm -f "$file"
        done < <(find . -name "$pattern" -type f -print0 2>/dev/null)
    done
    
    if [ "$found_files" = false ]; then
        echo -e "${CYAN}未找到临时文件${NC}"
    else
        echo -e "${GREEN}✅ 临时文件清理完成${NC}"
    fi
}

# 解析命令行参数
CLEAN_BUILD=false
CLEAN_LOGS=false
CLEAN_TEMP=false

if [ $# -eq 0 ]; then
    show_help
    exit 0
fi

while [[ $# -gt 0 ]]; do
    case $1 in
        --build)
            CLEAN_BUILD=true
            shift
            ;;
        --logs)
            CLEAN_LOGS=true
            shift
            ;;
        --temp)
            CLEAN_TEMP=true
            shift
            ;;
        --help|-h)
            show_help
            exit 0
            ;;
        *)
            echo -e "${RED}未知选项: $1${NC}"
            show_help
            exit 1
            ;;
    esac
done

# 显示当前项目大小
echo -e "\n${BLUE}--- 清理前状态 ---${NC}"
current_size=$(get_dir_size ".")
echo -e "当前项目总大小: ${CYAN}$current_size${NC}"

# 执行清理操作
if [ "$CLEAN_BUILD" = true ]; then
    clean_build
fi

if [ "$CLEAN_LOGS" = true ]; then
    clean_logs
fi

if [ "$CLEAN_TEMP" = true ]; then
    clean_temp
fi

# 显示清理后状态
echo -e "\n${BLUE}--- 清理完成 ---${NC}"
final_size=$(get_dir_size ".")
echo -e "清理后项目大小: ${CYAN}$final_size${NC}"
echo -e "${GREEN}✅ 项目清理完成！${NC}"
echo -e "${YELLOW}💡 提示: 运行 './build.sh' 重新编译项目${NC}"

echo -e "\n${BLUE}=== 清理完成 ($(date '+%Y-%m-%d %H:%M:%S')) ===${NC}"
