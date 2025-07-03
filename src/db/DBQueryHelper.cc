#include "db/DBQueryHelper.h"
#include <sstream>

namespace kama {
namespace db {

// SqlConditionBuilder实现
SqlConditionBuilder::SqlConditionBuilder() {
}

SqlConditionBuilder& SqlConditionBuilder::equal(const std::string& field, const std::string& value) {
    m_conditions.push_back(field + " = '" + escapeValue(value) + "'");
    return *this;
}

SqlConditionBuilder& SqlConditionBuilder::equal(const std::string& field, int value) {
    m_conditions.push_back(field + " = " + std::to_string(value));
    return *this;
}

SqlConditionBuilder& SqlConditionBuilder::notEqual(const std::string& field, const std::string& value) {
    m_conditions.push_back(field + " != '" + escapeValue(value) + "'");
    return *this;
}

SqlConditionBuilder& SqlConditionBuilder::notEqual(const std::string& field, int value) {
    m_conditions.push_back(field + " != " + std::to_string(value));
    return *this;
}

SqlConditionBuilder& SqlConditionBuilder::greaterThan(const std::string& field, const std::string& value) {
    m_conditions.push_back(field + " > '" + escapeValue(value) + "'");
    return *this;
}

SqlConditionBuilder& SqlConditionBuilder::greaterThan(const std::string& field, int value) {
    m_conditions.push_back(field + " > " + std::to_string(value));
    return *this;
}

SqlConditionBuilder& SqlConditionBuilder::lessThan(const std::string& field, const std::string& value) {
    m_conditions.push_back(field + " < '" + escapeValue(value) + "'");
    return *this;
}

SqlConditionBuilder& SqlConditionBuilder::lessThan(const std::string& field, int value) {
    m_conditions.push_back(field + " < " + std::to_string(value));
    return *this;
}

SqlConditionBuilder& SqlConditionBuilder::like(const std::string& field, const std::string& value) {
    m_conditions.push_back(field + " LIKE '" + escapeValue(value) + "'");
    return *this;
}

SqlConditionBuilder& SqlConditionBuilder::in(const std::string& field, const std::vector<std::string>& values) {
    if (values.empty()) {
        // 如果值为空，返回永假条件
        m_conditions.push_back("1 = 0");
        return *this;
    }
    
    std::stringstream ss;
    ss << field << " IN (";
    
    for (size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            ss << ", ";
        }
        ss << "'" << escapeValue(values[i]) << "'";
    }
    
    ss << ")";
    m_conditions.push_back(ss.str());
    
    return *this;
}

SqlConditionBuilder& SqlConditionBuilder::in(const std::string& field, const std::vector<int>& values) {
    if (values.empty()) {
        // 如果值为空，返回永假条件
        m_conditions.push_back("1 = 0");
        return *this;
    }
    
    std::stringstream ss;
    ss << field << " IN (";
    
    for (size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            ss << ", ";
        }
        ss << values[i];
    }
    
    ss << ")";
    m_conditions.push_back(ss.str());
    
    return *this;
}

SqlConditionBuilder& SqlConditionBuilder::customCondition(const std::string& condition) {
    m_conditions.push_back(condition);
    return *this;
}

std::string SqlConditionBuilder::build() const {
    if (m_conditions.empty()) {
        return "";
    }
    
    std::stringstream ss;
    
    for (size_t i = 0; i < m_conditions.size(); ++i) {
        if (i > 0) {
            ss << " AND ";
        }
        ss << "(" << m_conditions[i] << ")";
    }
    
    return ss.str();
}

bool SqlConditionBuilder::isEmpty() const {
    return m_conditions.empty();
}

std::string SqlConditionBuilder::escapeValue(const std::string& value) const {
    // 简单的SQL注入防护，实际应用中应使用MySQL提供的转义函数
    std::string result = value;
    
    size_t pos = 0;
    while ((pos = result.find('\'', pos)) != std::string::npos) {
        result.replace(pos, 1, "''");
        pos += 2;
    }
    
    return result;
}

// DBQueryHelper实现
DBQueryHelper::DBQueryHelper() {
    m_conn = DBConnectionPool::getInstance().getConnection();
    if (!m_conn) {
        m_lastError = "Failed to get database connection";
    }
}

bool DBQueryHelper::execute(const std::string& sql) {
    if (!m_conn) {
        m_conn = DBConnectionPool::getInstance().getConnection();
        if (!m_conn) {
            m_lastError = "No valid database connection";
            return false;
        }
    }
    
    bool success = m_conn->execute(sql);
    if (!success) {
        m_lastError = m_conn->getLastError();
    }
    
    return success;
}

bool DBQueryHelper::query(const std::string& sql, std::function<void(MYSQL_ROW row)> rowHandler) {
    if (!m_conn) {
        m_conn = DBConnectionPool::getInstance().getConnection();
        if (!m_conn) {
            m_lastError = "No valid database connection";
            return false;
        }
    }
    
    bool success = m_conn->query(sql, rowHandler);
    if (!success) {
        m_lastError = m_conn->getLastError();
    }
    
    return success;
}

std::string DBQueryHelper::queryScalar(const std::string& sql) {
    std::string result;
    
    queryOne(sql, [&](MYSQL_ROW row) {
        if (row[0]) {
            result = row[0];
        }
    });
    
    return result;
}

bool DBQueryHelper::queryOne(const std::string& sql, std::function<void(MYSQL_ROW row)> rowHandler) {
    if (!m_conn) {
        m_conn = DBConnectionPool::getInstance().getConnection();
        if (!m_conn) {
            m_lastError = "No valid database connection";
            return false;
        }
    }
    
    std::string limitedSql = sql;
    if (limitedSql.find(" LIMIT ") == std::string::npos) {
        limitedSql += " LIMIT 1";
    }
    
    bool foundRow = false;
    bool success = m_conn->query(limitedSql, [&](MYSQL_ROW row) {
        if (!foundRow) {
            rowHandler(row);
            foundRow = true;
        }
    });
    
    if (!success) {
        m_lastError = m_conn->getLastError();
    }
    
    return success;
}

int DBQueryHelper::queryCount(const std::string& tableName, const SqlConditionBuilder& conditions) {
    std::string sql = "SELECT COUNT(*) FROM " + tableName;
    
    std::string whereClause = conditions.build();
    if (!whereClause.empty()) {
        sql += " WHERE " + whereClause;
    }
    
    int count = -1;
    bool success = queryOne(sql, [&](MYSQL_ROW row) {
        if (row[0]) {
            count = std::stoi(row[0]);
        }
    });
    
    if (!success) {
        return -1;
    }
    
    return count;
}

bool DBQueryHelper::tableExists(const std::string& tableName) {
    std::string sql = "SHOW TABLES LIKE '" + tableName + "'";
    
    bool exists = false;
    bool success = queryOne(sql, [&](MYSQL_ROW row) {
        exists = true;
    });
    
    if (!success) {
        return false;
    }
    
    return exists;
}

std::shared_ptr<DBTransaction> DBQueryHelper::beginTransaction() {
    if (!m_conn) {
        m_conn = DBConnectionPool::getInstance().getConnection();
        if (!m_conn) {
            m_lastError = "No valid database connection";
            return nullptr;
        }
    }
    
    auto transaction = std::make_shared<DBTransaction>(m_conn);
    if (!transaction->begin()) {
        m_lastError = "Failed to begin transaction";
        return nullptr;
    }
    
    return transaction;
}

std::string DBQueryHelper::getLastError() const {
    return m_lastError;
}

// DBTransaction实现
DBTransaction::DBTransaction(std::shared_ptr<DBConnection> conn) 
    : m_conn(conn), m_active(false) {
}

DBTransaction::~DBTransaction() {
    if (m_active) {
        rollback();
    }
}

bool DBTransaction::begin() {
    if (!m_conn) return false;
    
    bool success = m_conn->execute("START TRANSACTION");
    if (success) {
        m_active = true;
    }
    
    return success;
}

bool DBTransaction::commit() {
    if (!m_conn || !m_active) return false;
    
    bool success = m_conn->execute("COMMIT");
    if (success) {
        m_active = false;
    }
    
    return success;
}

bool DBTransaction::rollback() {
    if (!m_conn || !m_active) return false;
    
    bool success = m_conn->execute("ROLLBACK");
    if (success) {
        m_active = false;
    }
    
    return success;
}

bool DBTransaction::execute(const std::string& sql) {
    if (!m_conn || !m_active) return false;
    
    return m_conn->execute(sql);
}

bool DBTransaction::query(const std::string& sql, std::function<void(MYSQL_ROW row)> rowHandler) {
    if (!m_conn || !m_active) return false;
    
    return m_conn->query(sql, rowHandler);
}

} // namespace db
} // namespace kama
