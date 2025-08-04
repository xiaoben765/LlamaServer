#pragma once

#include "services/IDatabaseService.h"
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <mysql/mysql.h>

namespace kama {
namespace services {

// 数据库连接配置
struct DbConfig {
    std::string host;
    int port;
    std::string dbname;
    std::string user;
    std::string password;
    std::string charset;
    
    DbConfig() : port(3306), charset("utf8mb4") {}
};

/**
 * @brief MySQL数据库服务实现
 * 
 * 实现IDatabaseService接口，使用MySQL作为后端存储
 */
class MySqlDatabaseService : public IDatabaseService {
public:
    // 单例模式，确保只有一个数据库连接实例
    static MySqlDatabaseService& instance();
    
    // IDatabaseService接口实现
    bool initialize() override;
    bool initialize(const DbConfig& config); // 扩展实现，支持配置参数
    bool isInitialized() const override { return initialized_; }
    void cleanup() override;
    
    // 新增的管理方法
    int clearCache() override;
    bool resetDatabase() override;
    bool clearTables(const std::vector<std::string>& tableNames) override;
    
    // 用户管理
    bool createUser(const std::string& username, const std::string& password, const std::string& email) override;
    bool authenticateUser(const std::string& username, const std::string& password) override;
    UserInfo getUserInfo(const std::string& username) override;
    bool updateUserLastLogin(const std::string& username) override;
    std::vector<UserInfo> getAllUsers() override;
    
    // 会话管理
    std::string createSession(const std::string& userId, const std::string& sessionName = "") override;
    bool updateSessionActivity(const std::string& sessionId) override;
    std::vector<SessionInfo> getUserSessions(const std::string& userId) override;
    SessionInfo getSessionInfo(const std::string& sessionId) override;
    bool deleteSession(const std::string& sessionId) override;
    bool sessionExists(const std::string& sessionId) override;
    
    // 对话记录
    bool saveConversation(const std::string& sessionId, const std::string& messageType, 
                         const std::string& content, const std::string& model = "", 
                         int promptTokens = 0, int completionTokens = 0) override;
    std::vector<ConversationRecord> getConversationHistory(const std::string& sessionId, int limit = 20, int offset = 0) override;
    int getConversationCount(const std::string& sessionId) override;
    bool deleteConversationHistory(const std::string& sessionId) override;
    
    // 缓存管理
    std::string getResponseFromCache(const std::string& query) override;
    bool saveToCache(const std::string& query, const std::string& response) override;
    void updateCacheStats(const std::string& query) override;
    void cleanupCache(int maxAgeHours = 24 * 7) override;
    std::vector<CacheRecord> getCacheStats(int limit = 100) override;
    
    // 配置管理
    std::string getConfig(const std::string& key, const std::string& defaultValue = "") override;
    bool setConfig(const std::string& key, const std::string& value) override;
    std::vector<std::pair<std::string, std::string>> getAllConfigs() override;
    
    // 统计信息
    nlohmann::json getSystemStats() override;
    int getCacheHits() const override;
    int getSessionsCount() const override;
    int getUsersCount() const override;
    int getConversationsCount() const override;
    long getCurrentTimestamp() const override;
    
    // 数据库状态相关方法
    int getUserCount();
    int getSessionCount();
    int getMessageCount();
    int getCacheCount();

private:
    MySqlDatabaseService();
    ~MySqlDatabaseService() override;
    
    // 禁用拷贝构造和赋值
    MySqlDatabaseService(const MySqlDatabaseService&) = delete;
    MySqlDatabaseService& operator=(const MySqlDatabaseService&) = delete;
    
    bool initialized_;
    DbConfig config_;
    MYSQL* connection_;
    mutable std::mutex mutex_;
    
    // 内部方法
    bool connect();
    void disconnect();
    bool reconnect();
    bool initializeTables();
    bool executeQuery(const std::string& query);
    MYSQL_RES* executeSelectQuery(const std::string& query);
    std::string escapeString(const std::string& str);
    std::string generateUUID();
    std::string hashString(const std::string& str);
};

/**
 * @brief 内存数据库服务实现（用于测试）
 * 
 * 实现IDatabaseService接口的内存版本，用于单元测试或不需要持久化的场景
 */
class MemoryDatabaseService : public IDatabaseService {
public:
    MemoryDatabaseService();
    ~MemoryDatabaseService() override = default;
    
    // IDatabaseService接口实现（简化版本，仅用于测试）
    bool initialize() override { initialized_ = true; return true; }
    bool isInitialized() const override { return initialized_; }
    void cleanup() override { /* 清空内存数据 */ }
    
    // 新增的管理方法
    int clearCache() override;
    bool resetDatabase() override;
    bool clearTables(const std::vector<std::string>& tableNames) override;
    
    // 用户管理
    bool createUser(const std::string& username, const std::string& password, const std::string& email) override;
    bool authenticateUser(const std::string& username, const std::string& password) override;
    UserInfo getUserInfo(const std::string& username) override;
    bool updateUserLastLogin(const std::string& username) override;
    std::vector<UserInfo> getAllUsers() override;
    
    // 会话管理
    std::string createSession(const std::string& userId, const std::string& sessionName = "") override;
    bool updateSessionActivity(const std::string& sessionId) override;
    std::vector<SessionInfo> getUserSessions(const std::string& userId) override;
    SessionInfo getSessionInfo(const std::string& sessionId) override;
    bool deleteSession(const std::string& sessionId) override;
    bool sessionExists(const std::string& sessionId) override;
    
    // 对话记录
    bool saveConversation(const std::string& sessionId, const std::string& messageType, 
                         const std::string& content, const std::string& model = "", 
                         int promptTokens = 0, int completionTokens = 0) override;
    std::vector<ConversationRecord> getConversationHistory(const std::string& sessionId, int limit = 20, int offset = 0) override;
    int getConversationCount(const std::string& sessionId) override;
    bool deleteConversationHistory(const std::string& sessionId) override;
    
    // 缓存管理
    std::string getResponseFromCache(const std::string& query) override;
    bool saveToCache(const std::string& query, const std::string& response) override;
    void updateCacheStats(const std::string& query) override;
    void cleanupCache(int maxAgeHours = 24 * 7) override;
    std::vector<CacheRecord> getCacheStats(int limit = 100) override;
    
    // 配置管理
    std::string getConfig(const std::string& key, const std::string& defaultValue = "") override;
    bool setConfig(const std::string& key, const std::string& value) override;
    std::vector<std::pair<std::string, std::string>> getAllConfigs() override;
    
    // 统计信息
    nlohmann::json getSystemStats() override;
    int getCacheHits() const override { return cacheHits_; }
    int getSessionsCount() const override { return sessions_.size(); }
    int getUsersCount() const override { return users_.size(); }
    int getConversationsCount() const override { return conversationsCount_; }
    long getCurrentTimestamp() const override;

private:
    bool initialized_;
    int cacheHits_;
    int conversationsCount_;
    
    // 内存存储
    std::unordered_map<std::string, UserInfo> users_;
    std::unordered_map<std::string, std::string> passwords_;  // username -> password
    std::unordered_map<std::string, SessionInfo> sessions_;
    std::unordered_map<std::string, std::vector<ConversationRecord>> conversations_;  // sessionId -> conversations
    std::unordered_map<std::string, CacheRecord> cache_;  // query_hash -> CacheRecord
    std::unordered_map<std::string, std::string> configs_;  // key -> value
    
    mutable std::mutex mutex_;
    std::string generateUUID();
    std::string hashString(const std::string& str);
};

} // namespace services
} // namespace kama
