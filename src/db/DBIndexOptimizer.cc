#include "db/DBIndexOptimizer.h"
#include <iostream>
#include <algorithm>
#include <regex>
#include <set>

namespace llama {
namespace db {

DBIndexOptimizer::DBIndexOptimizer() {
}

bool DBIndexOptimizer::getTableIndexes(const std::string& tableName, std::vector<IndexInfo>& indexes) {
    std::string sql = "SHOW INDEX FROM " + tableName;
    
    std::map<std::string, IndexInfo> indexMap;
    
    bool success = m_dbHelper.query(sql, [&](MYSQL_ROW row) {
        // 解析索引信息
        // row[0]: Table, row[1]: Non_unique, row[2]: Key_name
        // row[3]: Seq_in_index, row[4]: Column_name
        
        if (!row[2] || !row[4]) return;
        
        std::string indexName = row[2];
        std::string columnName = row[4];
        bool isUnique = (row[1] && std::string(row[1]) == "0");
        
        auto it = indexMap.find(indexName);
        if (it == indexMap.end()) {
            IndexInfo info;
            info.name = indexName;
            info.isUnique = isUnique;
            
            if (indexName == "PRIMARY") {
                info.type = IndexType::PRIMARY;
            } else if (isUnique) {
                info.type = IndexType::UNIQUE;
            } else {
                // 检查是否全文索引
                std::string indexType = row[10] ? row[10] : "";
                if (indexType == "FULLTEXT") {
                    info.type = IndexType::FULLTEXT;
                } else {
                    info.type = IndexType::NORMAL;
                }
            }
            
            info.columns.push_back(columnName);
            indexMap[indexName] = info;
        } else {
            it->second.columns.push_back(columnName);
        }
    });
    
    if (!success) {
        m_lastError = m_dbHelper.getLastError();
        return false;
    }
    
    // 将map转换为vector
    indexes.clear();
    for (const auto& pair : indexMap) {
        indexes.push_back(pair.second);
    }
    
    return true;
}

bool DBIndexOptimizer::indexExists(const std::string& tableName, const std::string& indexName) {
    std::vector<IndexInfo> indexes;
    if (!getTableIndexes(tableName, indexes)) {
        return false;
    }
    
    for (const auto& index : indexes) {
        if (index.name == indexName) {
            return true;
        }
    }
    
    return false;
}

bool DBIndexOptimizer::createIndex(const std::string& tableName, const std::string& indexName, 
                                  const std::vector<std::string>& columns, 
                                  IndexType type) {
    if (columns.empty()) {
        m_lastError = "No columns specified for index";
        return false;
    }
    
    if (indexExists(tableName, indexName)) {
        m_lastError = "Index already exists";
        return false;
    }
    
    std::string sql = "CREATE ";
    
    switch (type) {
    case IndexType::UNIQUE:
        sql += "UNIQUE ";
        break;
    case IndexType::PRIMARY:
        sql = "ALTER TABLE " + tableName + " ADD PRIMARY KEY";
        break;
    case IndexType::FULLTEXT:
        sql += "FULLTEXT ";
        break;
    default:
        break;
    }
    
    if (type != IndexType::PRIMARY) {
        sql += "INDEX " + indexName + " ON " + tableName;
    }
    
    // 添加列名
    sql += " (";
    for (size_t i = 0; i < columns.size(); ++i) {
        if (i > 0) {
            sql += ", ";
        }
        sql += columns[i];
    }
    sql += ")";
    
    bool success = m_dbHelper.execute(sql);
    if (!success) {
        m_lastError = m_dbHelper.getLastError();
    }
    
    return success;
}

bool DBIndexOptimizer::dropIndex(const std::string& tableName, const std::string& indexName) {
    if (indexName == "PRIMARY") {
        std::string sql = "ALTER TABLE " + tableName + " DROP PRIMARY KEY";
        bool success = m_dbHelper.execute(sql);
        if (!success) {
            m_lastError = m_dbHelper.getLastError();
        }
        return success;
    } else {
        std::string sql = "DROP INDEX " + indexName + " ON " + tableName;
        bool success = m_dbHelper.execute(sql);
        if (!success) {
            m_lastError = m_dbHelper.getLastError();
        }
        return success;
    }
}

bool DBIndexOptimizer::analyzeTableQueries(const std::string& tableName) {
    // 执行ANALYZE TABLE命令获取表统计信息
    std::string sql = "ANALYZE TABLE " + tableName;
    bool success = m_dbHelper.execute(sql);
    if (!success) {
        m_lastError = m_dbHelper.getLastError();
        return false;
    }
    
    return true;
}

std::vector<std::string> DBIndexOptimizer::getIndexSuggestions(const std::string& tableName) {
    std::vector<std::string> suggestions;
    
    // 查询表的列信息
    std::string sql = "SHOW COLUMNS FROM " + tableName;
    std::vector<std::string> columns;
    
    bool success = m_dbHelper.query(sql, [&](MYSQL_ROW row) {
        if (row[0]) {
            columns.push_back(row[0]);
        }
    });
    
    if (!success) {
        m_lastError = m_dbHelper.getLastError();
        return suggestions;
    }
    
    // 获取现有索引
    std::vector<IndexInfo> existingIndexes;
    if (!getTableIndexes(tableName, existingIndexes)) {
        return suggestions;
    }
    
    // 检查索引覆盖情况
    ::std::set<std::string> indexedColumns;
    for (const auto& index : existingIndexes) {
        for (const auto& col : index.columns) {
            indexedColumns.insert(col);
        }
    }
    
    // 检查是否有主键
    bool hasPrimaryKey = false;
    for (const auto& index : existingIndexes) {
        if (index.type == IndexType::PRIMARY) {
            hasPrimaryKey = true;
            break;
        }
    }
    
    if (!hasPrimaryKey) {
        suggestions.push_back("表没有主键，建议添加一个主键列");
    }
    
    // 检查常用查询列是否已索引
    // 这里可以根据实际业务需求添加更多建议
    
    return suggestions;
}

std::vector<std::string> DBIndexOptimizer::analyzeQuery(const std::string& sql) {
    std::vector<std::string> suggestions;
    
    // 使用正则表达式匹配WHERE子句中的条件
    std::regex whereRegex("\\bWHERE\\b\\s+(.+?)(?:\\bGROUP\\s+BY\\b|\\bORDER\\s+BY\\b|\\bLIMIT\\b|$)", 
                         std::regex_constants::icase);
    std::smatch whereMatch;
    
    if (std::regex_search(sql, whereMatch, whereRegex) && whereMatch.size() > 1) {
        std::string whereClause = whereMatch[1].str();
        
        // 提取WHERE子句中的列名
        std::regex columnRegex("\\b([a-zA-Z0-9_\\.]+)\\s*(?:=|>|<|>=|<=|<>|!=|LIKE|IN)");
        std::sregex_iterator it(whereClause.begin(), whereClause.end(), columnRegex);
        std::sregex_iterator end;
        
        ::std::set<std::string> columnsInWhere;
        while (it != end) {
            std::smatch match = *it;
            std::string column = match[1].str();
            
            // 移除表前缀
            size_t dotPos = column.find('.');
            if (dotPos != std::string::npos) {
                column = column.substr(dotPos + 1);
            }
            
            columnsInWhere.insert(column);
            ++it;
        }
        
        // 为WHERE子句中的列提供索引建议
        for (const auto& column : columnsInWhere) {
            suggestions.push_back("考虑为列 '" + column + "' 添加索引以优化WHERE条件");
        }
    }
    
    // 检查ORDER BY子句
    std::regex orderByRegex("\\bORDER\\s+BY\\b\\s+(.+?)(?:\\bLIMIT\\b|$)", 
                           std::regex_constants::icase);
    std::smatch orderByMatch;
    
    if (std::regex_search(sql, orderByMatch, orderByRegex) && orderByMatch.size() > 1) {
        std::string orderByClause = orderByMatch[1].str();
        
        // 提取ORDER BY子句中的列名
        std::regex columnRegex("\\b([a-zA-Z0-9_\\.]+)\\b");
        std::sregex_iterator it(orderByClause.begin(), orderByClause.end(), columnRegex);
        std::sregex_iterator end;
        
        std::vector<std::string> columnsInOrderBy;
        while (it != end) {
            std::smatch match = *it;
            std::string column = match[1].str();
            
            // 排除ASC/DESC关键字
            if (column != "ASC" && column != "DESC") {
                // 移除表前缀
                size_t dotPos = column.find('.');
                if (dotPos != std::string::npos) {
                    column = column.substr(dotPos + 1);
                }
                
                columnsInOrderBy.push_back(column);
            }
            
            ++it;
        }
        
        if (!columnsInOrderBy.empty()) {
            std::string suggestion = "考虑为ORDER BY中的列 (";
            for (size_t i = 0; i < columnsInOrderBy.size(); ++i) {
                if (i > 0) suggestion += ", ";
                suggestion += columnsInOrderBy[i];
            }
            suggestion += ") 创建索引";
            
            suggestions.push_back(suggestion);
        }
    }
    
    // 检查JOIN条件
    std::regex joinRegex("\\bJOIN\\b\\s+([a-zA-Z0-9_\\.]+)\\s+\\bON\\b\\s+(.+?)(?:\\bWHERE\\b|\\bGROUP\\s+BY\\b|\\bORDER\\s+BY\\b|\\bLIMIT\\b|\\bJOIN\\b|$)",
                        std::regex_constants::icase);
    std::sregex_iterator joinIt(sql.begin(), sql.end(), joinRegex);
    std::sregex_iterator end;
    
    while (joinIt != end) {
        std::smatch match = *joinIt;
        
        if (match.size() > 2) {
            std::string joinTable = match[1].str();
            std::string joinCondition = match[2].str();
            
            // 提取JOIN条件中的列名
            std::regex columnRegex("([a-zA-Z0-9_\\.]+)\\s*=\\s*([a-zA-Z0-9_\\.]+)");
            std::smatch columnMatch;
            
            if (std::regex_search(joinCondition, columnMatch, columnRegex) && columnMatch.size() > 2) {
                std::string column1 = columnMatch[1].str();
                std::string column2 = columnMatch[2].str();
                
                size_t dotPos1 = column1.find('.');
                size_t dotPos2 = column2.find('.');
                
                if (dotPos1 != std::string::npos && dotPos2 != std::string::npos) {
                    std::string table1 = column1.substr(0, dotPos1);
                    std::string col1 = column1.substr(dotPos1 + 1);
                    std::string table2 = column2.substr(0, dotPos2);
                    std::string col2 = column2.substr(dotPos2 + 1);
                    
                    suggestions.push_back("考虑为JOIN条件中的 " + table1 + "." + col1 + " 和 " + 
                                         table2 + "." + col2 + " 创建索引");
                }
            }
        }
        
        ++joinIt;
    }
    
    return suggestions;
}

std::string DBIndexOptimizer::getLastError() const {
    return m_lastError;
}

} // namespace db
} // namespace llama
