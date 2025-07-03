#include "db/DBConnectionPool.h"
#include "db/DBQueryHelper.h"
#include "db/DBIndexOptimizer.h"
#include <iomanip>  // 用于格式化输出
#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <thread>

using namespace kama::db;

// 打印性能统计信息
void printPerformanceStats(const std::string& operation, 
                         std::chrono::steady_clock::time_point start,
                         std::chrono::steady_clock::time_point end,
                         int iterations) {
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    std::cout << "操作: " << operation << std::endl;
    std::cout << "总时间: " << duration << " ms" << std::endl;
    std::cout << "平均时间: " << (double)duration / iterations << " ms" << std::endl;
    std::cout << "每秒操作数: " << (iterations * 1000.0) / duration << std::endl;
    std::cout << std::endl;
}

// 测试数据库连接池
void testConnectionPool() {
    std::cout << "===== 测试数据库连接池性能 =====" << std::endl;
    
    // 初始化连接池
    auto& pool = DBConnectionPool::getInstance();
    bool initialized = pool.init("localhost", "root", "password", "kama_llm", 3306, 10, 5);
    
    if (!initialized) {
        std::cerr << "连接池初始化失败" << std::endl;
        return;
    }
    
    std::cout << "连接池初始化成功" << std::endl;
    
    // 测试获取连接的性能
    const int iterations = 100;
    
    auto start = std::chrono::steady_clock::now();
    
    for (int i = 0; i < iterations; i++) {
        auto conn = pool.getConnection();
        if (conn) {
            pool.releaseConnection(conn);
        } else {
            std::cerr << "获取连接失败：第" << i << "次迭代" << std::endl;
        }
    }
    
    auto end = std::chrono::steady_clock::now();
    
    printPerformanceStats("获取和释放连接", start, end, iterations);
    
    std::cout << "当前活跃连接数: " << pool.getActiveConnectionCount() << std::endl;
    std::cout << "当前空闲连接数: " << pool.getIdleConnectionCount() << std::endl;
    std::cout << "当前总连接数: " << pool.getTotalConnectionCount() << std::endl;
    
    // 测试并发获取连接
    const int threadCount = 5;
    const int connPerThread = 20;
    std::vector<std::thread> threads;
    
    std::cout << "开始测试并发获取连接..." << std::endl;
    
    start = std::chrono::steady_clock::now();
    
    for (int t = 0; t < threadCount; t++) {
        threads.emplace_back([t, connPerThread]() {
            for (int i = 0; i < connPerThread; i++) {
                auto conn = DBConnectionPool::getInstance().getConnection();
                if (conn) {
                    // 模拟使用连接
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    DBConnectionPool::getInstance().releaseConnection(conn);
                } else {
                    std::cerr << "线程" << t << "获取连接失败：第" << i << "次迭代" << std::endl;
                }
            }
        });
    }
    
    // 等待所有线程完成
    for (auto& thread : threads) {
        thread.join();
    }
    
    end = std::chrono::steady_clock::now();
    
    printPerformanceStats("并发获取和释放连接", start, end, threadCount * connPerThread);
    
    std::cout << "当前活跃连接数: " << pool.getActiveConnectionCount() << std::endl;
    std::cout << "当前空闲连接数: " << pool.getIdleConnectionCount() << std::endl;
    std::cout << "当前总连接数: " << pool.getTotalConnectionCount() << std::endl;
    
    // 关闭连接池
    pool.close();
    std::cout << "连接池已关闭" << std::endl;
}

// 测试查询优化
void testQueryOptimization() {
    std::cout << "\n===== 测试查询优化性能 =====" << std::endl;
    
    // 初始化连接池
    auto& pool = DBConnectionPool::getInstance();
    pool.init("localhost", "root", "password", "kama_llm", 3306, 10, 5);
    
    DBQueryHelper queryHelper;
    
    // 创建测试表（如果不存在）
    std::string createTable = "CREATE TABLE IF NOT EXISTS performance_test ("
                            "id INT AUTO_INCREMENT PRIMARY KEY, "
                            "name VARCHAR(100), "
                            "value INT, "
                            "created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP)";
    
    if (!queryHelper.execute(createTable)) {
        std::cerr << "创建测试表失败: " << queryHelper.getLastError() << std::endl;
        return;
    }
    
    // 清空测试表
    queryHelper.execute("TRUNCATE TABLE performance_test");
    
    // 插入测试数据
    std::cout << "插入测试数据..." << std::endl;
    const int insertCount = 1000;
    auto start = std::chrono::steady_clock::now();
    
    for (int i = 0; i < insertCount; i++) {
        std::string insertSql = "INSERT INTO performance_test (name, value) "
                               "VALUES ('test_name_" + std::to_string(i) + "', " + 
                               std::to_string(i % 100) + ")";
        
        if (!queryHelper.execute(insertSql)) {
            std::cerr << "插入数据失败: " << queryHelper.getLastError() << std::endl;
        }
    }
    
    auto end = std::chrono::steady_clock::now();
    printPerformanceStats("插入数据", start, end, insertCount);
    
    // 测试无索引查询
    std::cout << "执行无索引查询..." << std::endl;
    const int queryCount = 100;
    start = std::chrono::steady_clock::now();
    
    for (int i = 0; i < queryCount; i++) {
        int value = i % 100;
        std::string sql = "SELECT * FROM performance_test WHERE value = " + std::to_string(value);
        queryHelper.query(sql, [](MYSQL_ROW) {
            // 只是计数，不处理结果
        });
    }
    
    end = std::chrono::steady_clock::now();
    printPerformanceStats("无索引查询", start, end, queryCount);
    
    // 创建索引
    std::cout << "创建索引..." << std::endl;
    queryHelper.execute("CREATE INDEX idx_value ON performance_test(value)");
    
    // 测试有索引查询
    std::cout << "执行有索引查询..." << std::endl;
    start = std::chrono::steady_clock::now();
    
    for (int i = 0; i < queryCount; i++) {
        int value = i % 100;
        std::string sql = "SELECT * FROM performance_test WHERE value = " + std::to_string(value);
        queryHelper.query(sql, [](MYSQL_ROW) {
            // 只是计数，不处理结果
        });
    }
    
    end = std::chrono::steady_clock::now();
    printPerformanceStats("有索引查询", start, end, queryCount);
    
    // 测试分页查询
    std::cout << "执行分页查询..." << std::endl;
    const int pageSize = 10;
    const int pageCount = 10;
    
    start = std::chrono::steady_clock::now();
    
    for (int page = 1; page <= pageCount; page++) {
        SqlConditionBuilder conditions;
        conditions.greaterThan("value", 50);
        
        PageResult<std::string> result;
        queryHelper.queryPage<std::string>(
            "performance_test",
            conditions,
            "id DESC",
            page,
            pageSize,
            [](MYSQL_ROW row) {
                return std::string(row[0]) + ":" + std::string(row[1]); // id:name
            },
            result
        );
        
        std::cout << "页码 " << page << ": 获取到 " << result.items.size() 
                 << " 条记录，总记录数: " << result.totalCount << std::endl;
    }
    
    end = std::chrono::steady_clock::now();
    printPerformanceStats("分页查询", start, end, pageCount);
    
    // 清理
    queryHelper.execute("DROP TABLE performance_test");
    pool.close();
}

// 测试索引优化
void testIndexOptimization() {
    std::cout << "\n===== 测试索引优化 =====" << std::endl;
    
    // 初始化连接池
    auto& pool = DBConnectionPool::getInstance();
    pool.init("localhost", "root", "password", "kama_llm", 3306, 10, 5);
    
    DBQueryHelper queryHelper;
    DBIndexOptimizer indexOptimizer;
    
    // 创建测试表（如果不存在）
    std::string createTable = "CREATE TABLE IF NOT EXISTS index_test ("
                            "id INT AUTO_INCREMENT PRIMARY KEY, "
                            "user_id INT, "
                            "product_id INT, "
                            "category VARCHAR(50), "
                            "price DECIMAL(10,2), "
                            "created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP)";
    
    if (!queryHelper.execute(createTable)) {
        std::cerr << "创建测试表失败: " << queryHelper.getLastError() << std::endl;
        return;
    }
    
    // 清空测试表
    queryHelper.execute("TRUNCATE TABLE index_test");
    
    // 插入测试数据
    std::cout << "插入测试数据..." << std::endl;
    const int insertCount = 1000;
    
    for (int i = 0; i < insertCount; i++) {
        int userId = i % 100;
        int productId = i % 200;
        std::string category = "category_" + std::to_string(i % 10);
        double price = i % 1000 + 0.99;
        
        std::string insertSql = "INSERT INTO index_test (user_id, product_id, category, price) "
                              "VALUES (" + std::to_string(userId) + ", " +
                              std::to_string(productId) + ", '" +
                              category + "', " +
                              std::to_string(price) + ")";
        
        if (!queryHelper.execute(insertSql)) {
            std::cerr << "插入数据失败: " << queryHelper.getLastError() << std::endl;
        }
    }
    
    // 获取表的现有索引
    std::vector<IndexInfo> indexes;
    if (indexOptimizer.getTableIndexes("index_test", indexes)) {
        std::cout << "当前表的索引：" << std::endl;
        for (const auto& index : indexes) {
            std::cout << " - " << index.name << " (";
            for (size_t i = 0; i < index.columns.size(); ++i) {
                if (i > 0) std::cout << ", ";
                std::cout << index.columns[i];
            }
            std::cout << ") - " << (index.isUnique ? "唯一" : "非唯一") << std::endl;
        }
    }
    
    // 分析查询语句，获取索引建议
    std::string complexQuery = "SELECT * FROM index_test "
                             "WHERE user_id = 10 AND category = 'category_5' "
                             "ORDER BY created_at DESC "
                             "LIMIT 10";
    
    std::vector<std::string> suggestions = indexOptimizer.analyzeQuery(complexQuery);
    
    std::cout << "\n针对查询的索引建议：" << std::endl;
    for (const auto& suggestion : suggestions) {
        std::cout << " - " << suggestion << std::endl;
    }
    
    // 创建建议的索引
    std::cout << "\n创建建议的索引..." << std::endl;
    indexOptimizer.createIndex("index_test", "idx_user_category", {"user_id", "category"});
    indexOptimizer.createIndex("index_test", "idx_created_at", {"created_at"});
    
    // 验证索引是否创建
    indexes.clear();
    if (indexOptimizer.getTableIndexes("index_test", indexes)) {
        std::cout << "创建索引后的索引列表：" << std::endl;
        for (const auto& index : indexes) {
            std::cout << " - " << index.name << " (";
            for (size_t i = 0; i < index.columns.size(); ++i) {
                if (i > 0) std::cout << ", ";
                std::cout << index.columns[i];
            }
            std::cout << ") - " << (index.isUnique ? "唯一" : "非唯一") << std::endl;
        }
    }
    
    // 测试索引对查询性能的影响
    std::cout << "\n测试索引对查询性能的影响..." << std::endl;
    const int queryCount = 50;
    
    auto start = std::chrono::steady_clock::now();
    
    for (int i = 0; i < queryCount; i++) {
        int userId = i % 100;
        std::string category = "category_" + std::to_string(i % 10);
        
        std::string sql = "SELECT * FROM index_test "
                       "WHERE user_id = " + std::to_string(userId) + 
                       " AND category = '" + category + "' "
                       "ORDER BY created_at DESC "
                       "LIMIT 10";
        
        queryHelper.query(sql, [](MYSQL_ROW) {
            // 只是计数，不处理结果
        });
    }
    
    auto end = std::chrono::steady_clock::now();
    printPerformanceStats("使用优化索引的查询", start, end, queryCount);
    
    // 清理
    std::cout << "清理测试表..." << std::endl;
    queryHelper.execute("DROP TABLE index_test");
    pool.close();
}

int main() {
    std::cout << "=== 数据库性能优化测试 ===" << std::endl;
    
    // 测试数据库连接池
    testConnectionPool();
    
    // 测试查询优化
    testQueryOptimization();
    
    // 测试索引优化
    testIndexOptimization();
    
    std::cout << "=== 测试完成 ===" << std::endl;
    return 0;
}
