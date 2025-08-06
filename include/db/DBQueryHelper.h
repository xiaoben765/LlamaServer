#ifndef LLAMA_DB_QUERY_HELPER_H
#define LLAMA_DB_QUERY_HELPER_H

#include "db/DBConnectionPool.h"
#include <string>
#include <vector>
#include <map>
#include <memory>

namespace llama {
namespace db {

// 前向声明
class DBTransaction;

/**
 * 分页查询结果
 */
template <typename T>
struct PageResult {
    std::vector<T> items;      // 当前页数据
    int pageIndex;             // 当前页码
    int pageSize;              // 页大小
    int totalCount;            // 总记录数
    int totalPages;            // 总页数
};

/**
 * SQL条件构造器
 * 用于构建WHERE子句的条件
 */
class SqlConditionBuilder {
public:
    SqlConditionBuilder();
    
    // 添加相等条件 (field = value)
    SqlConditionBuilder& equal(const std::string& field, const std::string& value);
    SqlConditionBuilder& equal(const std::string& field, int value);
    
    // 添加不等条件 (field != value)
    SqlConditionBuilder& notEqual(const std::string& field, const std::string& value);
    SqlConditionBuilder& notEqual(const std::string& field, int value);
    
    // 添加大于条件 (field > value)
    SqlConditionBuilder& greaterThan(const std::string& field, const std::string& value);
    SqlConditionBuilder& greaterThan(const std::string& field, int value);
    
    // 添加小于条件 (field < value)
    SqlConditionBuilder& lessThan(const std::string& field, const std::string& value);
    SqlConditionBuilder& lessThan(const std::string& field, int value);
    
    // 添加LIKE条件 (field LIKE value)
    SqlConditionBuilder& like(const std::string& field, const std::string& value);
    
    // 添加IN条件 (field IN (values))
    SqlConditionBuilder& in(const std::string& field, const std::vector<std::string>& values);
    SqlConditionBuilder& in(const std::string& field, const std::vector<int>& values);
    
    // 添加自定义条件
    SqlConditionBuilder& customCondition(const std::string& condition);
    
    // 获取最终的WHERE子句
    std::string build() const;
    
    // 判断是否有条件
    bool isEmpty() const;

private:
    std::string escapeValue(const std::string& value) const;

private:
    std::vector<std::string> m_conditions;
};

/**
 * 数据库查询助手
 * 提供常见的数据库操作方法，优化SQL查询
 */
class DBQueryHelper {
public:
    DBQueryHelper();
    
    // 执行单条SQL语句
    bool execute(const std::string& sql);
    
    // 执行查询并返回结果（由用户处理每一行结果）
    bool query(const std::string& sql, std::function<void(MYSQL_ROW row)> rowHandler);
    
    // 执行查询并返回第一行第一列
    std::string queryScalar(const std::string& sql);
    
    // 查询单条记录
    bool queryOne(const std::string& sql, std::function<void(MYSQL_ROW row)> rowHandler);
    
    // 分页查询
    template <typename T>
    bool queryPage(const std::string& tableName, const SqlConditionBuilder& conditions, 
                  const std::string& orderBy, int pageIndex, int pageSize,
                  std::function<T(MYSQL_ROW)> rowMapper, PageResult<T>& result);
    
    // 获取记录总数
    int queryCount(const std::string& tableName, const SqlConditionBuilder& conditions = SqlConditionBuilder());
    
    // 检查表是否存在
    bool tableExists(const std::string& tableName);
    
    // 创建或获取事务
    std::shared_ptr<DBTransaction> beginTransaction();
    
    // 获取上次错误信息
    std::string getLastError() const;

private:
    std::shared_ptr<DBConnection> m_conn;
    std::string m_lastError;
};

/**
 * 数据库事务类
 */
class DBTransaction {
public:
    DBTransaction(std::shared_ptr<DBConnection> conn);
    ~DBTransaction();
    
    bool begin();
    bool commit();
    bool rollback();
    
    // 执行SQL
    bool execute(const std::string& sql);
    
    // 查询
    bool query(const std::string& sql, std::function<void(MYSQL_ROW row)> rowHandler);

private:
    std::shared_ptr<DBConnection> m_conn;
    bool m_active;
};

// 模板方法实现
template <typename T>
bool DBQueryHelper::queryPage(const std::string& tableName, const SqlConditionBuilder& conditions, 
                             const std::string& orderBy, int pageIndex, int pageSize,
                             std::function<T(MYSQL_ROW)> rowMapper, PageResult<T>& result) {
    if (pageIndex < 1) pageIndex = 1;
    if (pageSize < 1) pageSize = 10;
    
    // 获取总记录数
    int totalCount = queryCount(tableName, conditions);
    if (totalCount < 0) {
        return false;
    }
    
    // 计算总页数
    int totalPages = (totalCount + pageSize - 1) / pageSize;
    
    // 构建分页查询SQL
    std::string sql = "SELECT * FROM " + tableName;
    
    std::string whereClause = conditions.build();
    if (!whereClause.empty()) {
        sql += " WHERE " + whereClause;
    }
    
    if (!orderBy.empty()) {
        sql += " ORDER BY " + orderBy;
    }
    
    int offset = (pageIndex - 1) * pageSize;
    sql += " LIMIT " + std::to_string(offset) + ", " + std::to_string(pageSize);
    
    // 执行查询
    std::vector<T> items;
    bool success = query(sql, [&](MYSQL_ROW row) {
        items.push_back(rowMapper(row));
    });
    
    if (!success) {
        return false;
    }
    
    // 填充结果
    result.items = std::move(items);
    result.pageIndex = pageIndex;
    result.pageSize = pageSize;
    result.totalCount = totalCount;
    result.totalPages = totalPages;
    
    return true;
}

} // namespace db
} // namespace llama

#endif // LLAMA_DB_QUERY_HELPER_H
