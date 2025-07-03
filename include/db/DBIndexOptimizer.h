#ifndef KAMA_DB_INDEX_OPTIMIZER_H
#define KAMA_DB_INDEX_OPTIMIZER_H

#include "db/DBQueryHelper.h"
#include <string>
#include <vector>
#include <map>

namespace kama {
namespace db {

/**
 * 索引类型枚举
 */
enum class IndexType {
    NORMAL,     // 普通索引
    UNIQUE,     // 唯一索引
    PRIMARY,    // 主键索引
    FULLTEXT    // 全文索引
};

/**
 * 索引信息结构体
 */
struct IndexInfo {
    std::string name;          // 索引名称
    IndexType type;            // 索引类型
    std::vector<std::string> columns;  // 包含的列
    bool isUnique;             // 是否唯一索引
};

/**
 * 数据库索引优化器
 * 用于分析查询模式，优化索引结构
 */
class DBIndexOptimizer {
public:
    DBIndexOptimizer();
    
    // 获取表的所有索引信息
    bool getTableIndexes(const std::string& tableName, std::vector<IndexInfo>& indexes);
    
    // 判断索引是否存在
    bool indexExists(const std::string& tableName, const std::string& indexName);
    
    // 创建索引
    bool createIndex(const std::string& tableName, const std::string& indexName, 
                    const std::vector<std::string>& columns, 
                    IndexType type = IndexType::NORMAL);
    
    // 删除索引
    bool dropIndex(const std::string& tableName, const std::string& indexName);
    
    // 分析表的查询性能
    bool analyzeTableQueries(const std::string& tableName);
    
    // 获取索引建议
    std::vector<std::string> getIndexSuggestions(const std::string& tableName);
    
    // 分析查询语句，提供索引优化建议
    std::vector<std::string> analyzeQuery(const std::string& sql);
    
    // 获取错误信息
    std::string getLastError() const;

private:
    DBQueryHelper m_dbHelper;
    std::string m_lastError;
};

} // namespace db
} // namespace kama

#endif // KAMA_DB_INDEX_OPTIMIZER_H
