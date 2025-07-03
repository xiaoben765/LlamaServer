# 🚀 Kama-WebServer 性能测试完整指南

本指南提供了全面的性能测试方案，帮助您评估和优化 Kama-WebServer 的性能表现。

---

## 📋 测试概览

### 🎯 测试目标
- **吞吐量**: 测试系统的请求处理能力 (QPS/TPS)
- **响应时间**: 测试各种负载下的响应延迟
- **并发能力**: 测试系统支持的最大并发用户数
- **稳定性**: 测试长时间运行的稳定性
- **资源利用**: 测试 CPU、内存、I/O 使用情况
- **可扩展性**: 测试负载增长时的性能表现

### 🛠️ 测试工具
- **Apache Bench (ab)**: HTTP 基准测试
- **wrk**: 现代 HTTP 基准测试工具
- **自定义测试工具**: C++ 编写的专用测试程序
- **系统监控**: sar, iostat, htop
- **数据库分析**: SQLite 性能分析

---

## 🔧 环境准备

### 1. 安装测试依赖
```bash
# 安装基础工具
sudo apt-get update
sudo apt-get install -y apache2-utils wrk sysstat htop sqlite3

# 安装开发依赖 (用于自定义测试工具)
sudo apt-get install -y libcurl4-openssl-dev libjsoncpp-dev

# 使用 Makefile 安装依赖
make -f Makefile.performance install-deps
```

### 2. 构建测试工具
```bash
# 构建自定义性能测试工具
make -f Makefile.performance bin/performance_tester

# 或者手动构建
g++ -std=c++17 -O2 -pthread -o bin/performance_tester src/performance_tester.cpp -lcurl -ljsoncpp
```

### 3. 启动服务器
```bash
# 启动 HTTP 服务器
./bin/main_http

# 或者使用模块化服务器
./bin/main_http_modular

# 或者使用启动脚本
./start_services.sh
```

---

## 🏃‍♂️ 快速开始

### 1. 快速性能测试 (3-5 分钟)
```bash
# 运行快速测试套件
./performance_test.sh --quick

# 或者使用 Makefile
make -f Makefile.performance test-quick
```

### 2. 完整性能测试 (15-20 分钟)
```bash
# 运行完整测试套件
./performance_test.sh

# 或者使用 Makefile
make -f Makefile.performance test-full
```

### 3. 压力测试 (30-60 分钟)
```bash
# 运行压力测试
./performance_test.sh --stress

# 或者使用 Makefile
make -f Makefile.performance test-stress
```

---

## 📊 详细测试方案

### 1. HTTP 性能测试

#### Apache Bench 测试
```bash
# GET 请求测试
ab -n 1000 -c 10 http://127.0.0.1:8080/api/status

# POST 请求测试
echo '{"message": "测试消息"}' > post_data.json
ab -n 1000 -c 10 -p post_data.json -T "application/json" \
   http://127.0.0.1:8080/api/chat
```

#### wrk 测试
```bash
# GET 请求测试
wrk -t4 -c20 -d60s --latency http://127.0.0.1:8080/api/status

# POST 请求测试 (需要 Lua 脚本)
wrk -t4 -c20 -d60s --latency -s post_script.lua \
    http://127.0.0.1:8080/api/chat
```

#### 自定义测试工具
```bash
# 运行自定义性能测试
./bin/performance_tester http://127.0.0.1:8080

# 指定不同的服务器地址
./bin/performance_tester http://your-server:8080
```

### 2. 并发模型测试

#### 异步任务队列测试
```bash
# 测试异步任务队列性能
./bin/test_concurrency --async
```

#### 模型实例池测试
```bash
# 测试模型实例池的负载均衡和故障转移
./bin/test_concurrency --pool
```

#### 异步服务测试
```bash
# 测试异步 LLaMA 服务
./bin/test_concurrency --service
```

### 3. 数据库性能测试
```bash
# 运行数据库性能测试
./test_database_performance.sh
```

### 4. 内存泄漏测试
```bash
# 使用 Valgrind 检测内存泄漏
valgrind --tool=memcheck --leak-check=full ./bin/main_http

# 长时间运行测试内存稳定性
./performance_test.sh --stress  # 包含内存监控
```

---

## 📈 性能指标解读

### 🎯 关键性能指标 (KPI)

#### 吞吐量指标
- **QPS (Queries Per Second)**: 每秒处理请求数
  - 目标: GET 请求 > 1000 QPS, POST 请求 > 500 QPS
- **TPS (Transactions Per Second)**: 每秒事务数
  - 目标: 数据库事务 > 200 TPS

#### 响应时间指标
- **平均响应时间**: 所有请求的平均处理时间
  - 目标: < 100ms (GET), < 200ms (POST)
- **P95 响应时间**: 95% 请求的响应时间
  - 目标: < 200ms (GET), < 500ms (POST)
- **P99 响应时间**: 99% 请求的响应时间
  - 目标: < 500ms (GET), < 1000ms (POST)

#### 并发性能指标
- **最大并发用户数**: 系统能稳定支持的并发用户
  - 目标: > 100 并发用户
- **连接成功率**: 连接建立成功的比例
  - 目标: > 99.5%

#### 系统资源指标
- **CPU 使用率**: 处理器使用情况
  - 目标: < 80% (平均), < 95% (峰值)
- **内存使用率**: 内存占用情况
  - 目标: < 70%, 无内存泄漏
- **I/O 性能**: 磁盘读写性能
  - 目标: I/O 等待 < 10%

### 📊 性能基准参考

| 测试场景 | 并发数 | QPS 目标 | 响应时间目标 | 成功率目标 |
|---------|--------|----------|-------------|-----------|
| 轻负载 | 1-10 | > 500 | < 50ms | > 99.9% |
| 中负载 | 10-50 | > 300 | < 100ms | > 99.5% |
| 重负载 | 50-100 | > 200 | < 200ms | > 99.0% |
| 极限负载 | 100-500 | > 100 | < 500ms | > 95.0% |

---

## 🔍 问题诊断

### 常见性能问题及解决方案

#### 1. QPS 低于预期
**可能原因:**
- 线程池配置过小
- 数据库连接不足
- 网络延迟高

**解决方案:**
```bash
# 检查线程配置
grep -r "thread" config/config.json

# 监控数据库连接
sqlite3 kama.db ".timeout 1000"

# 检查网络延迟
ping 127.0.0.1
```

#### 2. 响应时间过长
**可能原因:**
- LLaMA 模型处理慢
- 数据库查询未优化
- 缓存未命中

**解决方案:**
```bash
# 检查 LLaMA 服务状态
curl http://127.0.0.1:8899/status

# 分析数据库查询
sqlite3 kama.db "EXPLAIN QUERY PLAN SELECT ..."

# 检查缓存命中率
curl http://127.0.0.1:8080/api/status
```

#### 3. 内存使用过高
**可能原因:**
- 内存泄漏
- 缓存过大
- 连接池未释放

**解决方案:**
```bash
# 内存使用监控
ps aux | grep main_http
top -p $(pgrep main_http)

# 内存泄漏检测
valgrind --tool=memcheck ./bin/main_http
```

#### 4. 系统负载过高
**可能原因:**
- CPU 密集型操作
- I/O 瓶颈
- 并发控制不当

**解决方案:**
```bash
# 系统负载监控
htop
sar -u 1 10
iostat -x 1 10

# 调整并发配置
vim config/config.json
```

---

## 📝 性能测试报告

### 报告内容
运行测试后，会自动生成以下报告文件：

#### 1. 综合性能报告
- 文件: `performance_results/performance_report.md`
- 内容: 整体性能指标、系统资源使用、优化建议

#### 2. 数据库性能报告  
- 文件: `db_performance_results/database_performance_report.md`
- 内容: 数据库操作性能、查询分析、缓存效果

#### 3. 详细测试日志
- Apache Bench: `performance_results/ab_*.log`
- wrk: `performance_results/wrk_*.log`
- 系统监控: `performance_results/system_stats.log`
- 自定义测试: 控制台输出

### 报告分析技巧
```bash
# 提取关键指标
grep "Requests per second" performance_results/ab_*.log
grep "Latency" performance_results/wrk_*.log

# 分析响应时间分布
sort -n performance_results/ab_status_c10.tsv | tail -20

# 查看系统资源趋势
tail -50 performance_results/system_stats.log
```

---

## 🎯 性能优化建议

### 1. 应用层优化
- **连接池**: 配置合适的数据库连接池大小
- **缓存策略**: 增加热点数据缓存时间
- **异步处理**: 充分利用异步任务队列
- **负载均衡**: 配置多个 LLaMA 实例

### 2. 系统层优化
- **内核参数**: 调整 TCP 参数和文件描述符限制
- **内存配置**: 增加系统内存或优化内存使用
- **I/O 优化**: 使用 SSD 或调整 I/O 调度策略

### 3. 数据库优化
- **索引优化**: 为高频查询字段添加索引
- **查询优化**: 分析和优化慢查询
- **事务优化**: 合理使用事务批处理

### 4. 网络优化
- **Keep-Alive**: 启用 HTTP Keep-Alive
- **压缩**: 启用响应内容压缩
- **CDN**: 对于静态资源使用 CDN

---

## 🔄 持续性能监控

### 1. 定期性能测试
```bash
# 设置定时任务
crontab -e
# 添加: 0 2 * * 0 /path/to/performance_test.sh --quick > /var/log/weekly_perf_test.log
```

### 2. 生产环境监控
```bash
# 实时监控脚本
#!/bin/bash
while true; do
    curl -s http://127.0.0.1:8080/api/status | jq '.response_time'
    sleep 30
done
```

### 3. 性能趋势分析
- 建立性能基准数据库
- 定期比较性能指标
- 设置性能告警阈值

---

## ❓ 常见问题 FAQ

### Q: 测试时服务器无响应怎么办？
**A:** 检查服务器是否启动，端口是否正确，防火墙是否开放。

### Q: 性能测试结果不稳定怎么办？
**A:** 确保测试环境稳定，关闭其他占用资源的程序，多次测试取平均值。

### Q: 如何确定合适的并发数？
**A:** 从小并发开始逐步增加，找到响应时间急剧上升的拐点。

### Q: 内存使用持续增长是否正常？
**A:** 短期增长正常，但长期运行应趋于稳定，持续增长可能有内存泄漏。

---

## 📚 相关资源

- [Apache Bench 文档](https://httpd.apache.org/docs/2.4/programs/ab.html)
- [wrk 使用指南](https://github.com/wg/wrk)
- [SQLite 性能优化](https://www.sqlite.org/optoverview.html)
- [Linux 性能分析](https://brendangregg.com/perf.html)

---

*最后更新: $(date)*
*项目地址: /home/shl203/kama-webserver*
