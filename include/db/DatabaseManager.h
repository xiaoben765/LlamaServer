#pragma once

#include <string>
#include <vector>
#include <memory>
#include <mutex>  // 确保这行存在
#include <mysql/mysql.h>
#include "nlohmann/json.hpp"

using json = nlohmann::json;

namespace kama {
namespace db {

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
    std::string model_used;    // 添加这一行
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

// 数据库管理器类
class DatabaseManager {
public:
    static DatabaseManager& instance();
    
    bool initialize(const DbConfig& config);
    bool isInitialized() const { return initialized_; }
    void cleanup();
    
    // 用户管理
    bool createUser(const std::string& username, const std::string& password, const std::string& email);
    bool authenticateUser(const std::string& username, const std::string& password);
    UserInfo getUserInfo(const std::string& username);
    bool updateUserLastLogin(const std::string& username);
    std::vector<UserInfo> getAllUsers();
    
    // 会话管理
    std::string createSession(const std::string& userId, const std::string& sessionName = "");
    bool updateSessionActivity(const std::string& sessionId);
    std::vector<SessionInfo> getUserSessions(const std::string& userId);
    SessionInfo getSessionInfo(const std::string& sessionId);
    bool deleteSession(const std::string& sessionId);
    bool sessionExists(const std::string& sessionId); // 检查会话是否存在
    
    // 对话记录
    bool saveConversation(const std::string& sessionId, const std::string& messageType, 
                         const std::string& content, const std::string& model = "", 
                         int promptTokens = 0, int completionTokens = 0);
    std::vector<ConversationRecord> getConversationHistory(const std::string& sessionId, int limit = 20, int offset = 0);
    
    // 工具函数 - 已经在private中定义，无需重复
    int getConversationCount(const std::string& sessionId);
    bool deleteConversationHistory(const std::string& sessionId);
    
    // 缓存管理
    std::string getResponseFromCache(const std::string& query);
    bool saveToCache(const std::string& query, const std::string& response);
    void updateCacheStats(const std::string& query);
    void cleanupCache(int maxAgeHours = 24 * 7); // 默认一周
    std::vector<CacheRecord> getCacheStats(int limit = 100);
    
    // 配置管理
    std::string getConfig(const std::string& key, const std::string& defaultValue = "");
    bool setConfig(const std::string& key, const std::string& value);
    std::vector<std::pair<std::string, std::string>> getAllConfigs();
    
    // 统计信息
    json getSystemStats();
    int getCacheHits() const;
    int getSessionsCount() const;
    int getUsersCount() const;
    int getConversationsCount() const;
    
    // 将 getCurrentTimestamp 从 private 部分移动到这里
    long getCurrentTimestamp() const;

    // 数据库状态相关方法
    int getUserCount();
    int getSessionCount();
    int getMessageCount();
    int getCacheCount();

private:
    DatabaseManager();
    ~DatabaseManager();
    
    // 禁用拷贝构造和赋值
    DatabaseManager(const DatabaseManager&) = delete;
    DatabaseManager& operator=(const DatabaseManager&) = delete;
    
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

} // namespace db
} // namespace kama