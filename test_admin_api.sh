#!/bin/bash

# 颜色定义
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${YELLOW}===== 测试管理API =====${NC}"

# 测试缓存清除API
echo -e "${GREEN}测试缓存清除API${NC}"
echo "请求: POST /api/admin/clear-cache"
curl -X POST "http://localhost:8080/api/admin/clear-cache" -H "Content-Type: application/json" | json_pp
echo ""

# 测试数据库重置API
echo -e "${GREEN}测试数据库重置API${NC}"
echo "请求: POST /api/admin/clear-database"
curl -X POST "http://localhost:8080/api/admin/clear-database" -H "Content-Type: application/json" -d '{"tables": ["cache"]}' | json_pp
echo ""

# 测试系统状态API
echo -e "${GREEN}测试系统状态API${NC}"
echo "请求: GET /api/status"
curl "http://localhost:8080/api/status" | json_pp
echo ""

echo -e "${YELLOW}测试完成${NC}"
