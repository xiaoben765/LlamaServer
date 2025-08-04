#!/bin/bash

# 测试缓存清除API
echo "测试缓存清除API"
curl -X POST "http://localhost:8080/api/admin/clear-cache" -H "Content-Type: application/json"
echo ""
echo "测试完成"
