#!/bin/bash

# 添加颜色输出
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 默认配置文件路径
CONFIG_FILE="config/config.json"
BACKUP_DIR="config/backup"

# 创建备份目录
mkdir -p $BACKUP_DIR

# 函数：显示帮助信息
show_help() {
    echo -e "${BLUE}Kama WebServer 配置管理工具${NC}"
    echo "用法: $0 [选项]"
    echo ""
    echo "选项:"
    echo "  -s, --show          显示当前配置"
    echo "  -e, --edit          编辑配置文件"
    echo "  -b, --backup        备份当前配置"
    echo "  -r, --restore ID    恢复指定的配置备份"
    echo "  -l, --list          列出所有配置备份"
    echo "  -c, --check         检查配置文件语法"
    echo "  -h, --help          显示此帮助信息"
}

# 函数：显示当前配置
show_config() {
    if [ ! -f "$CONFIG_FILE" ]; then
        echo -e "${RED}配置文件不存在: $CONFIG_FILE${NC}"
        exit 1
    fi
    
    echo -e "${BLUE}=== 当前配置 ===${NC}"
    # 使用jq美化输出，如果jq不可用则使用cat
    if command -v jq > /dev/null; then
        jq . "$CONFIG_FILE"
    else
        cat "$CONFIG_FILE"
    fi
}

# 函数：编辑配置文件
edit_config() {
    if [ ! -f "$CONFIG_FILE" ]; then
        echo -e "${YELLOW}配置文件不存在，将创建新配置文件${NC}"
        mkdir -p $(dirname "$CONFIG_FILE")
    fi
    
    # 先备份当前配置
    backup_config "编辑前自动备份"
    
    # 使用默认编辑器打开配置文件
    if [ -n "$EDITOR" ]; then
        $EDITOR "$CONFIG_FILE"
    elif command -v nano > /dev/null; then
        nano "$CONFIG_FILE"
    elif command -v vim > /dev/null; then
        vim "$CONFIG_FILE"
    else
        echo -e "${RED}未找到可用的编辑器${NC}"
        return 1
    fi
    
    # 编辑后检查语法
    check_config
}

# 函数：备份当前配置
backup_config() {
    local comment=$1
    if [ ! -f "$CONFIG_FILE" ]; then
        echo -e "${RED}配置文件不存在，无法备份${NC}"
        return 1
    fi
    
    # 生成备份ID和文件名
    local timestamp=$(date +%Y%m%d_%H%M%S)
    local backup_id="${timestamp}"
    local backup_file="${BACKUP_DIR}/config_${backup_id}.json"
    
    # 复制配置文件
    cp "$CONFIG_FILE" "$backup_file"
    
    # 创建备份元数据
    echo "{\"id\":\"$backup_id\",\"timestamp\":\"$(date)\",\"comment\":\"$comment\"}" > "${BACKUP_DIR}/meta_${backup_id}.json"
    
    echo -e "${GREEN}配置已备份: $backup_id${NC}"
    echo "备份文件: $backup_file"
}

# 函数：列出所有备份
list_backups() {
    echo -e "${BLUE}=== 配置备份列表 ===${NC}"
    
    local meta_files=$(ls ${BACKUP_DIR}/meta_*.json 2>/dev/null)
    if [ -z "$meta_files" ]; then
        echo "无可用备份"
        return
    fi
    
    echo -e "ID\t\t时间\t\t\t\t备注"
    echo "---------------------------------------------------------"
    
    for meta_file in $meta_files; do
        if command -v jq > /dev/null; then
            local id=$(jq -r '.id' "$meta_file")
            local timestamp=$(jq -r '.timestamp' "$meta_file")
            local comment=$(jq -r '.comment' "$meta_file")
            echo -e "$id\t$timestamp\t$comment"
        else
            echo "$(basename "$meta_file" | sed 's/meta_//' | sed 's/.json//')"
        fi
    done
}

# 函数：恢复备份
restore_backup() {
    local backup_id=$1
    local backup_file="${BACKUP_DIR}/config_${backup_id}.json"
    local meta_file="${BACKUP_DIR}/meta_${backup_id}.json"
    
    if [ ! -f "$backup_file" ] || [ ! -f "$meta_file" ]; then
        echo -e "${RED}找不到指定的备份: $backup_id${NC}"
        list_backups
        return 1
    fi
    
    # 先备份当前配置
    backup_config "恢复前自动备份"
    
    # 恢复备份
    cp "$backup_file" "$CONFIG_FILE"
    
    # 检查配置文件语法
    check_config
    
    echo -e "${GREEN}已恢复配置: $backup_id${NC}"
}

# 函数：检查配置文件语法
check_config() {
    if [ ! -f "$CONFIG_FILE" ]; then
        echo -e "${RED}配置文件不存在: $CONFIG_FILE${NC}"
        return 1
    fi
    
    if command -v jq > /dev/null; then
        if jq . "$CONFIG_FILE" > /dev/null; then
            echo -e "${GREEN}配置文件语法检查通过${NC}"
            return 0
        else
            echo -e "${RED}配置文件存在语法错误${NC}"
            return 1
        fi
    else
        echo -e "${YELLOW}警告: 无法使用jq进行JSON语法检查${NC}"
        echo "建议安装jq: sudo apt install jq"
    fi
}

# 处理命令行参数
if [ $# -eq 0 ]; then
    show_help
    exit 0
fi

while [ $# -gt 0 ]; do
    case $1 in
        -h|--help)
            show_help
            exit 0
            ;;
        -s|--show)
            show_config
            shift
            ;;
        -e|--edit)
            edit_config
            shift
            ;;
        -b|--backup)
            if [ -n "$2" ] && [ "${2:0:1}" != "-" ]; then
                backup_config "$2"
                shift 2
            else
                backup_config "手动备份"
                shift
            fi
            ;;
        -l|--list)
            list_backups
            shift
            ;;
        -r|--restore)
            if [ -n "$2" ] && [ "${2:0:1}" != "-" ]; then
                restore_backup "$2"
                shift 2
            else
                echo -e "${RED}错误: 恢复需要指定备份ID${NC}"
                list_backups
                exit 1
            fi
            ;;
        -c|--check)
            check_config
            shift
            ;;
        *)
            echo -e "${RED}未知选项: $1${NC}"
            show_help
            exit 1
            ;;
    esac
done

exit 0
