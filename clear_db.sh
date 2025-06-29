#!/bin/bash

# 数据库连接信息
DB_USER="root"
DB_PASS="password"
DB_NAME="kama_llm"

# 显示彩色输出
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

echo -e "${YELLOW}正在清空数据库 $DB_NAME 中的所有表...${NC}"

# 获取所有表名
TABLES=$(mysql -u$DB_USER -p$DB_PASS -e "USE $DB_NAME; SHOW TABLES;" -s --skip-column-names)

# 禁用外键检查（避免因外键约束而无法清空表）
mysql -u$DB_USER -p$DB_PASS -e "USE $DB_NAME; SET FOREIGN_KEY_CHECKS=0;"

# 循环清空每个表
for TABLE in $TABLES; do
    echo -e "清空表: ${GREEN}$TABLE${NC}"
    mysql -u$DB_USER -p$DB_PASS -e "USE $DB_NAME; TRUNCATE TABLE $TABLE;"
done

# 重新启用外键检查
mysql -u$DB_USER -p$DB_PASS -e "USE $DB_NAME; SET FOREIGN_KEY_CHECKS=1;"

echo -e "${GREEN}所有表已清空完成!${NC}"

# 显示表状态
echo -e "${YELLOW}数据库表状态:${NC}"
mysql -u$DB_USER -p$DB_PASS -e "USE $DB_NAME; SHOW TABLE STATUS;" | grep -v "+---"
