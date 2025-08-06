#pragma once

#include <string>
#include <vector>
#include <memory>
#include "nlohmann/json.hpp"

namespace llama {
namespace services {

// 以下结构体定义与DatabaseManager中相同，为了接口独立性复制到这里
// 用户信息结构
struct UserInfo {
    std::string user_id;
    std::string username;
    std::string email;
    long created_at;
    long last_login;
};

// 会话信息结构
struct SessionInfo {
    std::string session_id;
    std::string user_id;
    long created_at;
    long last_active;
    std::string session_name;
};

// 对话记录结构
struct ConversationRecord {
    std::string message_id;
    std::string session_id;
    std::string message_type;  // "user" 或 "ai"
    std::string content;
    long timestamp;
    std::string model_used;    
    std::string model;
    int prompt_tokens;
    int completion_tokens;
};

// 缓存记录结构
struct CacheRecord {
    std::string query_hash;
    std::string query_text;
    std::string response_text;
    long created_at;
    int hit_count;
    long last_accessed;
};

/**
 * @brief 数据库服务接口
 * 
 * 定义了与数据库交互的标准接口，
 * 允许不同的实现（如MySQL、SQLite、Mock等）
 */
class IDatabaseService {
public:
    virtual ~IDatabaseService() = default;
    
    // 初始化和状态
    virtual bool initialize() = 0;
    virtual bool isInitialized() const = 0;
    virtual void cleanup() = 0;
    
    // 用户管理
    virtual bool createUser(const std::string& username, const std::string& password, const std::string& email) = 0;
    virtual bool authenticateUser(const std::string& username, const std::string& password) = 0;
    virtual UserInfo getUserInfo(const std::string& username) = 0;
    virtual bool updateUserLastLogin(const std::string& username) = 0;
    virtual std::vector<UserInfo> getAllUsers() = 0;
    virtual bool deleteUser(const std::string& username) = 0;
    
    // 会话管理
    virtual std::string createSession(const std::string& userId, const std::string& sessionName = "") = 0;
    virtual bool updateSessionActivity(const std::string& sessionId) = 0;
    virtual std::vector<SessionInfo> getUserSessions(const std::string& userId) = 0;
    virtual SessionInfo getSessionInfo(const std::string& sessionId) = 0;
    virtual bool deleteSession(const std::string& sessionId) = 0;
    virtual bool sessionExists(const std::string& sessionId) = 0;
    
    // 对话记录
    virtual bool saveConversation(const std::string& sessionId, const std::string& messageType, 
                         const std::string& content, const std::string& model = "", 
                         int promptTokens = 0, int completionTokens = 0) = 0;
    virtual std::vector<ConversationRecord> getConversationHistory(const std::string& sessionId, int limit = 20, int offset = 0) = 0;
    virtual int getConversationCount(const std::string& sessionId) = 0;
    virtual bool deleteConversationHistory(const std::string& sessionId) = 0;
    
    // 缓存管理
    virtual std::string getResponseFromCache(const std::string& query) = 0;
    virtual bool saveToCache(const std::string& query, const std::string& response) = 0;
    virtual void updateCacheStats(const std::string& query) = 0;
    virtual void cleanupCache(int maxAgeHours = 24 * 7) = 0;
    virtual std::vector<CacheRecord> getCacheStats(int limit = 100) = 0;
    virtual int clearCache() = 0; // 清除所有缓存条目，返回被清除的条目数
    virtual bool resetDatabase() = 0; // 重置整个数据库（清除所有表）
    virtual bool clearTables(const std::vector<std::string>& tableNames) = 0; // 清除指定表
    
    // 配置管理
    virtual std::string getConfig(const std::string& key, const std::string& defaultValue = "") = 0;
    virtual bool setConfig(const std::string& key, const std::string& value) = 0;
    virtual std::vector<std::pair<std::string, std::string>> getAllConfigs() = 0;
    
    // 统计信息
    virtual nlohmann::json getSystemStats() = 0;
    virtual int getCacheHits() const = 0;
    virtual int getSessionsCount() const = 0;
    virtual int getUsersCount() const = 0;
    virtual int getConversationsCount() const = 0;
    virtual long getCurrentTimestamp() const = 0;
};

} // namespace services
} // namespace llama
