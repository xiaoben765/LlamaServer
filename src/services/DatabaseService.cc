#include "services/DatabaseService.h"
#include "db/DatabaseManager.h"  // 依赖原有的DatabaseManager实现
#include "Logger.h"
#include <chrono>
#include <random>     // 为std::random_device和std::mt19937
#include <sstream>    // 为std::stringstream

namespace kama {
namespace services {

// 单例模式实现
MySqlDatabaseService& MySqlDatabaseService::instance() {
    static MySqlDatabaseService instance;
    return instance;
}

MySqlDatabaseService::MySqlDatabaseService()
    : initialized_(false), connection_(nullptr) {
}

MySqlDatabaseService::~MySqlDatabaseService() {
    cleanup();
}

bool MySqlDatabaseService::initialize() {
    // 使用默认配置进行初始化
    DbConfig config;
    config.host = "localhost";
    config.port = 3306;
    config.dbname = "kama";
    config.user = "root";
    config.password = ""; // 默认空密码
    
    return initialize(config);
}

bool MySqlDatabaseService::initialize(const DbConfig& config) {
    // 使用现有的DatabaseManager进行初始化
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (initialized_) {
        LOG_INFO << "数据库服务已经初始化";
        return true;
    }
    
    // 转换配置为DatabaseManager需要的格式
    db::DbConfig dbConfig;
    dbConfig.host = config.host;
    dbConfig.port = config.port;
    dbConfig.dbname = config.dbname;
    dbConfig.user = config.user;
    dbConfig.password = config.password;
    dbConfig.charset = config.charset;
    
    // 调用原有的DatabaseManager初始化
    bool result = db::DatabaseManager::instance().initialize(dbConfig);
    if (result) {
        LOG_INFO << "数据库服务初始化成功: " << config.host << ":" << config.port << "/" << config.dbname;
        config_ = config;
        initialized_ = true;
    } else {
        LOG_ERROR << "数据库服务初始化失败";
    }
    
    return result;
}

void MySqlDatabaseService::cleanup() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (initialized_) {
        db::DatabaseManager::instance().cleanup();
        initialized_ = false;
        LOG_INFO << "数据库服务已清理";
    }
}

// 用户管理方法 - 转发到DatabaseManager

bool MySqlDatabaseService::createUser(const std::string& username, const std::string& password, const std::string& email) {
    if (!initialized_) return false;
    return db::DatabaseManager::instance().createUser(username, password, email);
}

bool MySqlDatabaseService::authenticateUser(const std::string& username, const std::string& password) {
    if (!initialized_) return false;
    return db::DatabaseManager::instance().authenticateUser(username, password);
}

UserInfo MySqlDatabaseService::getUserInfo(const std::string& username) {
    if (!initialized_) return UserInfo();
    
    // 转换DatabaseManager返回的结构到我们的接口结构
    db::UserInfo dbUserInfo = db::DatabaseManager::instance().getUserInfo(username);
    
    UserInfo userInfo;
    userInfo.user_id = dbUserInfo.user_id;
    userInfo.username = dbUserInfo.username;
    userInfo.email = dbUserInfo.email;
    userInfo.created_at = dbUserInfo.created_at;
    userInfo.last_login = dbUserInfo.last_login;
    
    return userInfo;
}

bool MySqlDatabaseService::updateUserLastLogin(const std::string& username) {
    if (!initialized_) return false;
    return db::DatabaseManager::instance().updateUserLastLogin(username);
}

std::vector<UserInfo> MySqlDatabaseService::getAllUsers() {
    std::vector<UserInfo> users;
    if (!initialized_) return users;
    
    // 转换DatabaseManager返回的结构到我们的接口结构
    auto dbUsers = db::DatabaseManager::instance().getAllUsers();
    users.reserve(dbUsers.size());
    
    for (const auto& dbUser : dbUsers) {
        UserInfo user;
        user.user_id = dbUser.user_id;
        user.username = dbUser.username;
        user.email = dbUser.email;
        user.created_at = dbUser.created_at;
        user.last_login = dbUser.last_login;
        users.push_back(user);
    }
    
    return users;
}

// 会话管理 - 转发到DatabaseManager

std::string MySqlDatabaseService::createSession(const std::string& userId, const std::string& sessionName) {
    if (!initialized_) return "";
    return db::DatabaseManager::instance().createSession(userId, sessionName);
}

bool MySqlDatabaseService::updateSessionActivity(const std::string& sessionId) {
    if (!initialized_) return false;
    return db::DatabaseManager::instance().updateSessionActivity(sessionId);
}

std::vector<SessionInfo> MySqlDatabaseService::getUserSessions(const std::string& userId) {
    std::vector<SessionInfo> sessions;
    if (!initialized_) return sessions;
    
    auto dbSessions = db::DatabaseManager::instance().getUserSessions(userId);
    sessions.reserve(dbSessions.size());
    
    for (const auto& dbSession : dbSessions) {
        SessionInfo session;
        session.session_id = dbSession.session_id;
        session.user_id = dbSession.user_id;
        session.created_at = dbSession.created_at;
        session.last_active = dbSession.last_active;
        session.session_name = dbSession.session_name;
        sessions.push_back(session);
    }
    
    return sessions;
}

SessionInfo MySqlDatabaseService::getSessionInfo(const std::string& sessionId) {
    if (!initialized_) return SessionInfo();
    
    auto dbSessionInfo = db::DatabaseManager::instance().getSessionInfo(sessionId);
    
    SessionInfo sessionInfo;
    sessionInfo.session_id = dbSessionInfo.session_id;
    sessionInfo.user_id = dbSessionInfo.user_id;
    sessionInfo.created_at = dbSessionInfo.created_at;
    sessionInfo.last_active = dbSessionInfo.last_active;
    sessionInfo.session_name = dbSessionInfo.session_name;
    
    return sessionInfo;
}

bool MySqlDatabaseService::deleteSession(const std::string& sessionId) {
    if (!initialized_) return false;
    return db::DatabaseManager::instance().deleteSession(sessionId);
}

bool MySqlDatabaseService::sessionExists(const std::string& sessionId) {
    if (!initialized_) return false;
    return db::DatabaseManager::instance().sessionExists(sessionId);
}

// 对话记录 - 转发到DatabaseManager

bool MySqlDatabaseService::saveConversation(const std::string& sessionId, const std::string& messageType, 
                                         const std::string& content, const std::string& model, 
                                         int promptTokens, int completionTokens) {
    if (!initialized_) return false;
    return db::DatabaseManager::instance().saveConversation(
        sessionId, messageType, content, model, promptTokens, completionTokens);
}

std::vector<ConversationRecord> MySqlDatabaseService::getConversationHistory(const std::string& sessionId, int limit, int offset) {
    std::vector<ConversationRecord> records;
    if (!initialized_) return records;
    
    auto dbRecords = db::DatabaseManager::instance().getConversationHistory(sessionId, limit, offset);
    records.reserve(dbRecords.size());
    
    for (const auto& dbRecord : dbRecords) {
        ConversationRecord record;
        record.message_id = dbRecord.message_id;
        record.session_id = dbRecord.session_id;
        record.message_type = dbRecord.message_type;
        record.content = dbRecord.content;
        record.timestamp = dbRecord.timestamp;
        record.model_used = dbRecord.model_used;
        record.model = dbRecord.model;
        record.prompt_tokens = dbRecord.prompt_tokens;
        record.completion_tokens = dbRecord.completion_tokens;
        records.push_back(record);
    }
    
    return records;
}

int MySqlDatabaseService::getConversationCount(const std::string& sessionId) {
    if (!initialized_) return 0;
    return db::DatabaseManager::instance().getConversationCount(sessionId);
}

bool MySqlDatabaseService::deleteConversationHistory(const std::string& sessionId) {
    if (!initialized_) return false;
    return db::DatabaseManager::instance().deleteConversationHistory(sessionId);
}

// 缓存管理 - 转发到DatabaseManager

std::string MySqlDatabaseService::getResponseFromCache(const std::string& query) {
    if (!initialized_) return "";
    return db::DatabaseManager::instance().getResponseFromCache(query);
}

bool MySqlDatabaseService::saveToCache(const std::string& query, const std::string& response) {
    if (!initialized_) return false;
    return db::DatabaseManager::instance().saveToCache(query, response);
}

void MySqlDatabaseService::updateCacheStats(const std::string& query) {
    if (!initialized_) return;
    db::DatabaseManager::instance().updateCacheStats(query);
}

void MySqlDatabaseService::cleanupCache(int maxAgeHours) {
    if (!initialized_) return;
    db::DatabaseManager::instance().cleanupCache(maxAgeHours);
}

std::vector<CacheRecord> MySqlDatabaseService::getCacheStats(int limit) {
    std::vector<CacheRecord> records;
    if (!initialized_) return records;
    
    auto dbRecords = db::DatabaseManager::instance().getCacheStats(limit);
    records.reserve(dbRecords.size());
    
    for (const auto& dbRecord : dbRecords) {
        CacheRecord record;
        record.query_hash = dbRecord.query_hash;
        record.query_text = dbRecord.query_text;
        record.response_text = dbRecord.response_text;
        record.created_at = dbRecord.created_at;
        record.hit_count = dbRecord.hit_count;
        record.last_accessed = dbRecord.last_accessed;
        records.push_back(record);
    }
    
    return records;
}

// 配置管理 - 转发到DatabaseManager

std::string MySqlDatabaseService::getConfig(const std::string& key, const std::string& defaultValue) {
    if (!initialized_) return defaultValue;
    return db::DatabaseManager::instance().getConfig(key, defaultValue);
}

bool MySqlDatabaseService::setConfig(const std::string& key, const std::string& value) {
    if (!initialized_) return false;
    return db::DatabaseManager::instance().setConfig(key, value);
}

std::vector<std::pair<std::string, std::string>> MySqlDatabaseService::getAllConfigs() {
    if (!initialized_) return {};
    return db::DatabaseManager::instance().getAllConfigs();
}

// 统计信息 - 转发到DatabaseManager

nlohmann::json MySqlDatabaseService::getSystemStats() {
    if (!initialized_) return nlohmann::json::object();
    return db::DatabaseManager::instance().getSystemStats();
}

int MySqlDatabaseService::getCacheHits() const {
    if (!initialized_) return 0;
    return db::DatabaseManager::instance().getCacheHits();
}

int MySqlDatabaseService::getSessionsCount() const {
    if (!initialized_) return 0;
    return db::DatabaseManager::instance().getSessionsCount();
}

int MySqlDatabaseService::getUsersCount() const {
    if (!initialized_) return 0;
    return db::DatabaseManager::instance().getUsersCount();
}

int MySqlDatabaseService::getConversationsCount() const {
    if (!initialized_) return 0;
    return db::DatabaseManager::instance().getConversationsCount();
}

long MySqlDatabaseService::getCurrentTimestamp() const {
    if (!initialized_) return 0;
    return db::DatabaseManager::instance().getCurrentTimestamp();
}

// 数据库状态相关方法

int MySqlDatabaseService::getUserCount() {
    if (!initialized_) return 0;
    return db::DatabaseManager::instance().getUserCount();
}

int MySqlDatabaseService::getSessionCount() {
    if (!initialized_) return 0;
    return db::DatabaseManager::instance().getSessionCount();
}

int MySqlDatabaseService::getMessageCount() {
    if (!initialized_) return 0;
    return db::DatabaseManager::instance().getMessageCount();
}

int MySqlDatabaseService::getCacheCount() {
    if (!initialized_) return 0;
    return db::DatabaseManager::instance().getCacheCount();
}

// MemoryDatabaseService实现 - 简单的内存存储版本用于测试

MemoryDatabaseService::MemoryDatabaseService()
    : initialized_(false), cacheHits_(0), conversationsCount_(0) {
}

long MemoryDatabaseService::getCurrentTimestamp() const {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string MemoryDatabaseService::generateUUID() {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    static std::uniform_int_distribution<> dis2(8, 11);

    std::stringstream ss;
    int i;
    ss << std::hex;
    for (i = 0; i < 8; i++) {
        ss << dis(gen);
    }
    ss << "-";
    for (i = 0; i < 4; i++) {
        ss << dis(gen);
    }
    ss << "-4";
    for (i = 0; i < 3; i++) {
        ss << dis(gen);
    }
    ss << "-";
    ss << dis2(gen);
    for (i = 0; i < 3; i++) {
        ss << dis(gen);
    }
    ss << "-";
    for (i = 0; i < 12; i++) {
        ss << dis(gen);
    };
    return ss.str();
}

std::string MemoryDatabaseService::hashString(const std::string& str) {
    // 简单的哈希实现，生产环境应使用更安全的哈希
    std::hash<std::string> hasher;
    auto hash = hasher(str);
    std::stringstream ss;
    ss << std::hex << hash;
    return ss.str();
}

// 以下是MemoryDatabaseService的简单实现，仅用于测试

bool MemoryDatabaseService::createUser(const std::string& username, const std::string& password, const std::string& email) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 检查用户是否已存在
    if (passwords_.find(username) != passwords_.end()) {
        return false;
    }
    
    // 创建新用户
    UserInfo user;
    user.user_id = generateUUID();
    user.username = username;
    user.email = email;
    user.created_at = getCurrentTimestamp();
    user.last_login = user.created_at;
    
    users_[username] = user;
    passwords_[username] = hashString(password); // 实际应用中应使用更安全的哈希
    
    return true;
}

bool MemoryDatabaseService::authenticateUser(const std::string& username, const std::string& password) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = passwords_.find(username);
    if (it == passwords_.end()) {
        return false;
    }
    
    return it->second == hashString(password);
}

UserInfo MemoryDatabaseService::getUserInfo(const std::string& username) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = users_.find(username);
    if (it == users_.end()) {
        return UserInfo();
    }
    
    return it->second;
}

bool MemoryDatabaseService::updateUserLastLogin(const std::string& username) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = users_.find(username);
    if (it == users_.end()) {
        return false;
    }
    
    it->second.last_login = getCurrentTimestamp();
    return true;
}

std::vector<UserInfo> MemoryDatabaseService::getAllUsers() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<UserInfo> result;
    result.reserve(users_.size());
    
    for (const auto& pair : users_) {
        result.push_back(pair.second);
    }
    
    return result;
}

// 会话管理的简单实现

std::string MemoryDatabaseService::createSession(const std::string& userId, const std::string& sessionName) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    SessionInfo session;
    session.session_id = generateUUID();
    session.user_id = userId;
    session.created_at = getCurrentTimestamp();
    session.last_active = session.created_at;
    session.session_name = sessionName.empty() ? "新会话 " + std::to_string(sessions_.size() + 1) : sessionName;
    
    sessions_[session.session_id] = session;
    
    return session.session_id;
}

bool MemoryDatabaseService::updateSessionActivity(const std::string& sessionId) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = sessions_.find(sessionId);
    if (it == sessions_.end()) {
        return false;
    }
    
    it->second.last_active = getCurrentTimestamp();
    return true;
}

std::vector<SessionInfo> MemoryDatabaseService::getUserSessions(const std::string& userId) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<SessionInfo> result;
    
    for (const auto& pair : sessions_) {
        if (pair.second.user_id == userId) {
            result.push_back(pair.second);
        }
    }
    
    return result;
}

SessionInfo MemoryDatabaseService::getSessionInfo(const std::string& sessionId) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = sessions_.find(sessionId);
    if (it == sessions_.end()) {
        return SessionInfo();
    }
    
    return it->second;
}

bool MemoryDatabaseService::deleteSession(const std::string& sessionId) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = sessions_.find(sessionId);
    if (it == sessions_.end()) {
        return false;
    }
    
    // 删除会话及相关对话记录
    sessions_.erase(it);
    conversations_.erase(sessionId);
    
    return true;
}

bool MemoryDatabaseService::sessionExists(const std::string& sessionId) {
    std::lock_guard<std::mutex> lock(mutex_);
    return sessions_.find(sessionId) != sessions_.end();
}

// 对话记录的简单实现

bool MemoryDatabaseService::saveConversation(const std::string& sessionId, const std::string& messageType, 
                                         const std::string& content, const std::string& model, 
                                         int promptTokens, int completionTokens) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 检查会话是否存在
    if (sessions_.find(sessionId) == sessions_.end()) {
        return false;
    }
    
    ConversationRecord record;
    record.message_id = generateUUID();
    record.session_id = sessionId;
    record.message_type = messageType;
    record.content = content;
    record.timestamp = getCurrentTimestamp();
    record.model_used = model;
    record.model = model;
    record.prompt_tokens = promptTokens;
    record.completion_tokens = completionTokens;
    
    conversations_[sessionId].push_back(record);
    conversationsCount_++;
    
    return true;
}

std::vector<ConversationRecord> MemoryDatabaseService::getConversationHistory(const std::string& sessionId, int limit, int offset) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<ConversationRecord> result;
    
    auto it = conversations_.find(sessionId);
    if (it == conversations_.end()) {
        return result;
    }
    
    const auto& records = it->second;
    
    // 应用偏移和限制
    int start = std::max(0, static_cast<int>(records.size()) - offset - limit);
    int end = std::min(static_cast<int>(records.size()), start + limit);
    
    for (int i = start; i < end; i++) {
        result.push_back(records[i]);
    }
    
    return result;
}

int MemoryDatabaseService::getConversationCount(const std::string& sessionId) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = conversations_.find(sessionId);
    if (it == conversations_.end()) {
        return 0;
    }
    
    return it->second.size();
}

bool MemoryDatabaseService::deleteConversationHistory(const std::string& sessionId) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = conversations_.find(sessionId);
    if (it == conversations_.end()) {
        return false;
    }
    
    conversationsCount_ -= it->second.size();
    conversations_.erase(it);
    
    return true;
}

// 缓存管理的简单实现

std::string MemoryDatabaseService::getResponseFromCache(const std::string& query) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::string hash = hashString(query);
    auto it = cache_.find(hash);
    
    if (it == cache_.end()) {
        return ""; // 缓存未命中
    }
    
    // 更新缓存统计
    it->second.hit_count++;
    it->second.last_accessed = getCurrentTimestamp();
    cacheHits_++;
    
    return it->second.response_text;
}

bool MemoryDatabaseService::saveToCache(const std::string& query, const std::string& response) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::string hash = hashString(query);
    
    // 检查是否已存在
    auto it = cache_.find(hash);
    if (it != cache_.end()) {
        // 更新现有缓存
        it->second.response_text = response;
        it->second.last_accessed = getCurrentTimestamp();
        return true;
    }
    
    // 创建新缓存记录
    CacheRecord record;
    record.query_hash = hash;
    record.query_text = query;
    record.response_text = response;
    record.created_at = getCurrentTimestamp();
    record.hit_count = 0;
    record.last_accessed = record.created_at;
    
    cache_[hash] = record;
    
    return true;
}

void MemoryDatabaseService::updateCacheStats(const std::string& query) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::string hash = hashString(query);
    auto it = cache_.find(hash);
    
    if (it != cache_.end()) {
        it->second.hit_count++;
        it->second.last_accessed = getCurrentTimestamp();
        cacheHits_++;
    }
}

void MemoryDatabaseService::cleanupCache(int maxAgeHours) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    long cutoff = getCurrentTimestamp() - (maxAgeHours * 3600);
    
    // 删除过期缓存
    for (auto it = cache_.begin(); it != cache_.end();) {
        if (it->second.last_accessed < cutoff) {
            it = cache_.erase(it);
        } else {
            ++it;
        }
    }
}

std::vector<CacheRecord> MemoryDatabaseService::getCacheStats(int limit) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<CacheRecord> result;
    result.reserve(std::min(limit, static_cast<int>(cache_.size())));
    
    for (const auto& pair : cache_) {
        if (result.size() >= static_cast<size_t>(limit)) {
            break;
        }
        result.push_back(pair.second);
    }
    
    return result;
}

// 配置管理的简单实现

std::string MemoryDatabaseService::getConfig(const std::string& key, const std::string& defaultValue) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = configs_.find(key);
    if (it == configs_.end()) {
        return defaultValue;
    }
    
    return it->second;
}

bool MemoryDatabaseService::setConfig(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    configs_[key] = value;
    return true;
}

std::vector<std::pair<std::string, std::string>> MemoryDatabaseService::getAllConfigs() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::vector<std::pair<std::string, std::string>> result;
    result.reserve(configs_.size());
    
    for (const auto& pair : configs_) {
        result.emplace_back(pair.first, pair.second);
    }
    
    return result;
}

// 统计信息的简单实现

nlohmann::json MemoryDatabaseService::getSystemStats() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    nlohmann::json stats;
    stats["users_count"] = users_.size();
    stats["sessions_count"] = sessions_.size();
    stats["conversations_count"] = conversationsCount_;
    stats["cache_count"] = cache_.size();
    stats["cache_hits"] = cacheHits_;
    stats["timestamp"] = getCurrentTimestamp();
    
    return stats;
}

} // namespace services
} // namespace kama
