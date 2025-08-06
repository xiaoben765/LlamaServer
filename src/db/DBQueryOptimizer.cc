#include "db/DBQueryOptimizer.h"
#include <iostream>
#include <algorithm>
#include <regex>
#include <cctype>

namespace llama {
namespace db {

DBQueryOptimizer& DBQueryOptimizer::getInstance() {
    static DBQueryOptimizer instance;
    return instance;
}

DBQueryOptimizer::DBQueryOptimizer() {
}

DBQueryOptimizer::~DBQueryOptimizer() {
}

std::string DBQueryOptimizer::optimizeQuery(const std::string& sql) {
    std::string result = sql;
    
    // 基本清理
    result = cleanupSql(result);
    
    // 检查查询类型
    std::string upperSql = result;
    std::transform(upperSql.begin(), upperSql.end(), upperSql.begin(), ::toupper);
    
    if (upperSql.find("SELECT") == 0) {
        // 优化SELECT查询
        
        // 1. 避免使用SELECT *
        if (upperSql.find("SELECT *") == 0 || upperSql.find("SELECT DISTINCT *") == 0) {
            std::cerr << "Warning: Using SELECT * is not recommended. Specify required columns instead." << std::endl;
        }
        
        // 2. 优化WHERE子句
        size_t wherePos = upperSql.find("WHERE");
        if (wherePos != std::string::npos) {
            size_t groupByPos = upperSql.find("GROUP BY", wherePos);
            size_t orderByPos = upperSql.find("ORDER BY", wherePos);
            size_t limitPos = upperSql.find("LIMIT", wherePos);
            
            size_t endPos = std::string::npos;
            if (groupByPos != std::string::npos) endPos = groupByPos;
            else if (orderByPos != std::string::npos) endPos = orderByPos;
            else if (limitPos != std::string::npos) endPos = limitPos;
            
            if (endPos != std::string::npos) {
                std::string whereClause = result.substr(wherePos + 5, endPos - wherePos - 5);
                std::string optimizedWhere = optimizeWhereClause(whereClause);
                result.replace(wherePos + 5, endPos - wherePos - 5, optimizedWhere);
            }
        }
        
        // 3. 确保使用了索引（仅提示，实际优化需要索引分析器）
        std::vector<std::string> tables = extractTableNames(result);
        if (!tables.empty()) {
            // 这里可以针对具体表检查索引使用情况
            // 在实际产品中，应该与索引分析器集成
        }
    }
    
    return result;
}

std::string DBQueryOptimizer::buildPageQuery(const std::string& baseQuery, const PageParams& params) {
    // 确保基本查询不包含LIMIT
    std::string upperBaseQuery = baseQuery;
    std::transform(upperBaseQuery.begin(), upperBaseQuery.end(), upperBaseQuery.begin(), ::toupper);
    if (upperBaseQuery.find("LIMIT") != std::string::npos) {
        std::cerr << "Warning: Base query should not contain LIMIT clause for pagination." << std::endl;
    }
    
    std::string pageQuery = baseQuery;
    
    // 添加ORDER BY子句（如果提供）
    if (!params.orderBy.empty()) {
        // 检查基本查询是否已包含ORDER BY
        if (upperBaseQuery.find("ORDER BY") == std::string::npos) {
            pageQuery += " ORDER BY " + params.orderBy;
            pageQuery += params.isAsc ? " ASC" : " DESC";
        } else {
            std::cerr << "Warning: Base query already contains ORDER BY clause." << std::endl;
        }
    }
    
    // 添加LIMIT子句
    size_t offset = (params.page - 1) * params.pageSize;
    pageQuery += " LIMIT " + std::to_string(offset) + ", " + std::to_string(params.pageSize);
    
    return pageQuery;
}

bool DBQueryOptimizer::executePageQuery(const std::string& baseQuery, const PageParams& params, 
                                      std::function<void(MYSQL_ROW)> rowHandler) {
    // 获取连接池实例
    auto& pool = DBConnectionPool::getInstance();
    
    // 从连接池获取连接
    auto conn = pool.getConnection();
    if (!conn) {
        std::cerr << "Failed to get database connection for page query" << std::endl;
        return false;
    }
    
    // 构建分页查询
    std::string pageQuery = buildPageQuery(baseQuery, params);
    
    // 执行查询
    bool success = conn->query(pageQuery, rowHandler);
    
    // 释放连接回连接池
    pool.releaseConnection(conn);
    
    return success;
}

size_t DBQueryOptimizer::getQueryCount(const std::string& baseQuery) {
    // 获取连接池实例
    auto& pool = DBConnectionPool::getInstance();
    
    // 从连接池获取连接
    auto conn = pool.getConnection();
    if (!conn) {
        std::cerr << "Failed to get database connection for count query" << std::endl;
        return 0;
    }
    
    // 构建COUNT查询
    std::string countQuery = buildCountQuery(baseQuery);
    
    // 执行查询
    size_t count = 0;
    conn->query(countQuery, [&count](MYSQL_ROW row) {
        if (row[0]) {
            count = std::stoul(row[0]);
        }
    });
    
    // 释放连接回连接池
    pool.releaseConnection(conn);
    
    return count;
}

bool DBQueryOptimizer::isSqlSafe(const std::string& sql) {
    // 检查SQL注入攻击模式
    static const std::vector<std::string> dangerousPatterns = {
        "--", "/*", "*/", "@@", "@variable", 
        "EXEC", "EXECUTE", "UNION", "INSERT", "DROP", "ALTER", 
        "TRUNCATE", "DELETE", "UPDATE", "GRANT", "REVOKE"
    };
    
    std::string upperSql = sql;
    std::transform(upperSql.begin(), upperSql.end(), upperSql.begin(), ::toupper);
    
    // 检查危险模式
    for (const auto& pattern : dangerousPatterns) {
        std::string upperPattern = pattern;
        std::transform(upperPattern.begin(), upperPattern.end(), upperPattern.begin(), ::toupper);
        
        // 如果是SELECT查询，某些模式是允许的
        if (upperSql.find("SELECT") == 0 && 
            (upperPattern == "UPDATE" || upperPattern == "INSERT" || 
             upperPattern == "DELETE" || upperPattern == "UNION")) {
            continue;
        }
        
        if (upperSql.find(upperPattern) != std::string::npos) {
            return false;
        }
    }
    
    // 检查多语句执行
    if (upperSql.find(";") != std::string::npos && 
        upperSql.find(";") != upperSql.length() - 1) {
        return false;
    }
    
    return true;
}

std::string DBQueryOptimizer::cleanupSql(const std::string& sql) {
    std::string result = sql;
    
    // 移除多余的空白字符
    std::regex multipleSpaces("\\s+");
    result = std::regex_replace(result, multipleSpaces, " ");
    
    // 修复常见格式问题
    std::regex selectFrom("SELECT\\s+FROM");
    result = std::regex_replace(result, selectFrom, "SELECT * FROM");
    
    // 移除前后空白
    result = std::regex_replace(result, std::regex("^\\s+"), "");
    result = std::regex_replace(result, std::regex("\\s+$"), "");
    
    return result;
}

std::vector<std::string> DBQueryOptimizer::extractTableNames(const std::string& sql) {
    std::vector<std::string> tables;
    
    // 简单提取表名的正则表达式
    // 注意：这只是一个基本实现，实际上SQL解析更复杂
    std::regex tablePattern("FROM\\s+([\\w\\._]+)|JOIN\\s+([\\w\\._]+)", 
                           std::regex_constants::icase);
    
    std::sregex_iterator it(sql.begin(), sql.end(), tablePattern);
    std::sregex_iterator end;
    
    while (it != end) {
        std::smatch match = *it;
        if (match[1].matched) {
            tables.push_back(match[1].str());
        } else if (match[2].matched) {
            tables.push_back(match[2].str());
        }
        ++it;
    }
    
    return tables;
}

std::string DBQueryOptimizer::optimizeWhereClause(const std::string& whereClause) {
    std::string result = whereClause;
    
    // 这里可以实现更复杂的WHERE子句优化
    // 例如：调整条件顺序，确保索引字段在前
    
    return result;
}

bool DBQueryOptimizer::hasAggregateFunction(const std::string& sql) {
    std::regex aggPattern("(COUNT|SUM|AVG|MIN|MAX)\\s*\\(", 
                         std::regex_constants::icase);
    return std::regex_search(sql, aggPattern);
}

std::string DBQueryOptimizer::buildCountQuery(const std::string& baseQuery) {
    // 将原查询转为大写以便分析
    std::string upperQuery = baseQuery;
    std::transform(upperQuery.begin(), upperQuery.end(), upperQuery.begin(), ::toupper);
    
    // 检查是否是简单查询
    bool isSimpleQuery = true;
    if (upperQuery.find("GROUP BY") != std::string::npos || 
        upperQuery.find("HAVING") != std::string::npos ||
        hasAggregateFunction(upperQuery)) {
        isSimpleQuery = false;
    }
    
    if (isSimpleQuery) {
        // 简单查询，替换选择列表为COUNT(*)
        size_t selectPos = upperQuery.find("SELECT");
        size_t fromPos = upperQuery.find("FROM");
        
        if (selectPos != std::string::npos && fromPos != std::string::npos) {
            return "SELECT COUNT(*) " + baseQuery.substr(fromPos);
        }
    }
    
    // 复杂查询，使用子查询
    return "SELECT COUNT(*) FROM (" + baseQuery + ") AS count_tbl";
}

} // namespace db
} // namespace llama
