#ifndef LLAMA_DB_QUERY_OPTIMIZER_H
#define LLAMA_DB_QUERY_OPTIMIZER_H

#include <string>
#include <vector>
#include <map>
#include <memory>
#include "db/DBConnectionPool.h"

namespace llama {
namespace db {

/**
 * 分页查询结果
 */
template<typename T>
struct PageResult {
    std::vector<T> data;         // 当前页数据
    size_t totalCount;           // 总记录数
    size_t pageSize;             // 每页大小
    size_t currentPage;          // 当前页码
    size_t totalPages;           // 总页数
};

/**
 * 分页参数
 */
struct PageParams {
    size_t page;                 // 页码，从1开始
    size_t pageSize;             // 每页大小
    std::string orderBy;         // 排序字段
    bool isAsc;                  // 是否升序
    
    PageParams() : page(1), pageSize(10), isAsc(true) {}
};

/**
 * 查询优化器类
 * 提供SQL查询优化、分页查询等功能
 */
class DBQueryOptimizer {
public:
    static DBQueryOptimizer& getInstance();
    
    /**
     * 优化基本的SQL查询
     * @param sql 原始SQL查询语句
     * @return 优化后的SQL查询语句
     */
    std::string optimizeQuery(const std::string& sql);
    
    /**
     * 生成分页SQL查询
     * @param baseQuery 基本查询（不含ORDER BY, LIMIT等）
     * @param params 分页参数
     * @return 分页查询SQL
     */
    std::string buildPageQuery(const std::string& baseQuery, const PageParams& params);
    
    /**
     * 执行分页查询
     * @param baseQuery 基本查询（不含ORDER BY, LIMIT等）
     * @param params 分页参数
     * @param rowHandler 行数据处理函数
     * @return 是否成功
     */
    bool executePageQuery(const std::string& baseQuery, const PageParams& params, 
                         std::function<void(MYSQL_ROW)> rowHandler);
    
    /**
     * 获取查询结果总数
     * @param baseQuery 基本查询
     * @return 查询结果总数
     */
    size_t getQueryCount(const std::string& baseQuery);
    
    /**
     * 检查SQL语句是否安全
     * 防止SQL注入
     * @param sql SQL语句
     * @return 是否安全
     */
    bool isSqlSafe(const std::string& sql);
    
    /**
     * 清理SQL查询语句
     * 移除不必要的部分、格式化
     * @param sql SQL语句
     * @return 清理后的SQL语句
     */
    std::string cleanupSql(const std::string& sql);
    
private:
    DBQueryOptimizer();
    ~DBQueryOptimizer();
    
    /**
     * 从查询中提取表名
     */
    std::vector<std::string> extractTableNames(const std::string& sql);
    
    /**
     * 优化WHERE子句
     */
    std::string optimizeWhereClause(const std::string& whereClause);
    
    /**
     * 检查是否包含聚合函数
     */
    bool hasAggregateFunction(const std::string& sql);
    
    /**
     * 构建COUNT查询
     */
    std::string buildCountQuery(const std::string& baseQuery);
};

} // namespace db
} // namespace llama

#endif // LLAMA_DB_QUERY_OPTIMIZER_H
