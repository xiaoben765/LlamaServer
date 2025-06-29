#pragma execution_character_set("utf-8")
#include "http/HttpServer.h"
#include "AsyncLogging.h"
#include "memoryPool.h"
#include "LFU.h"
#include "EventLoop.h"
#include "InetAddress.h"
#include "Logger.h"
#include "db/DatabaseManager.h"  // 添加数据库管理器头文件
#include <iostream>
#include <sys/stat.h>
#include <sstream>
#include <regex>

#include <fcntl.h>       // 提供 fcntl、F_GETFL、F_SETFL 定义
#include <unistd.h>      // 提供读写和关闭文件描述符的函数
#include <sys/socket.h>  // 套接字操作
#include <netinet/in.h>  // 互联网地址族
#include <arpa/inet.h>   // inet_pton 等函数
#include <errno.h>       // errno 和 strerror
#include <chrono>        // 用于时间相关操作
#include <thread>        // 用于 sleep_for
#include <cstring>       // 用于 strerror
#include <random>        // 用于随机数生成
#include <iomanip>       // 用于格式化
#include <map>           // 用于会话管理

#include "nlohmann/json.hpp"  // 如果安装在标准位置

using namespace kama::http;
// 添加命名空间
using json = nlohmann::json;  // 增加这行以使用简短名称

// 前向声明
std::string generateUUID();
std::string buildPromptWithHistory(const std::vector<std::string>& history);
void cleanInvalidUtf8(std::string& str);
void replaceAll(std::string& str, const std::string& from, const std::string& to);

// 修复正则表达式相关问题
std::string parseJsonMessage(const std::string& json) {
    std::regex messageRegex("\"message\"\\s*:\\s*\"([^\"]*)\"");
    std::smatch match;
    if (std::regex_search(json, match, messageRegex)) {
        return match[1].str();
    }
    return "";
}

// 修改buildJsonResponse函数实现
std::string buildJsonResponse(const std::string& message, bool cached) {
    json response;
    response["response"] = message;
    response["cached"] = cached;
    return response.dump();
}

std::string buildJsonStatus(int cacheSize, bool llamaAvailable) {
    std::ostringstream json;
    json << "{"
         << "\"cache_size\": " << cacheSize << ","
         << "\"llama_available\": " << (llamaAvailable ? "true" : "false") << ","
         << "\"timestamp\": " << time(nullptr)
         << "}";
    return json.str();
}

// 将LlamaService类修改为真实实现
class LlamaService {
public:
    LlamaService(const std::string& modelPath) 
        : modelPath_(modelPath),
          serverIp_("127.0.0.1"),  // 默认连接到本地LLaMA服务
          serverPort_(8899)        // 端口与llama_service_tcp.cc中的PORT一致
    {
        // 初始化时检查服务可用性
        available_ = checkServiceAvailable();
        if (available_) {
            LOG_INFO << "LLaMA 服务连接成功：" << serverIp_ << ":" << serverPort_;
        } else {
            LOG_ERROR << "LLaMA 服务不可用：" << serverIp_ << ":" << serverPort_;
        }
    }
    
    ~LlamaService() {
        if (persistent_sock_ > 0) {
            close(persistent_sock_);
        }
    }
    
    bool isServiceAvailable() const {
        return available_;
    }
    
    std::string query(const std::string& message) {
        // 尝试重新连接
        if (!ensureConnection()) {
            LOG_ERROR << "无法连接到 LLaMA 服务";
            return "服务暂时不可用，请稍后重试";
        }
        
        try {
            // 发送请求
            if (send(persistent_sock_, message.c_str(), message.length(), 0) < 0) {
                LOG_ERROR << "发送请求失败: " << strerror(errno);
                closeConnection();
                return "发送请求失败，请重试";
            }
            
            // 接收响应 - 增强版
            std::string response;
            char buffer[8192]; // 更大的缓冲区
            ssize_t bytes_read;
            
            auto start_time = std::chrono::steady_clock::now();
            bool reading = true;
            
            // 设置接收超时
            struct timeval tv;
            tv.tv_sec = 30; // 30秒超时
            tv.tv_usec = 0;
            setsockopt(persistent_sock_, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
            
            while (reading) {
                bytes_read = recv(persistent_sock_, buffer, sizeof(buffer) - 1, 0);
                
                if (bytes_read > 0) {
                    buffer[bytes_read] = '\0';
                    response += buffer;
                    
                    // 尝试判断是否接收完整 - 检查JSON结束
                    if (response.find("}") != std::string::npos && 
                        response.rfind("}") == response.length() - 1) {
                        break;
                    }
                    
                } else if (bytes_read == 0) {
                    // 连接关闭
                    LOG_INFO << "LLaMA 服务连接已关闭";
                    closeConnection();
                    break;
                } else {
                    if (errno != EAGAIN && errno != EWOULDBLOCK) {
                        LOG_ERROR << "接收数据错误: " << strerror(errno);
                        closeConnection();
                        break;
                    } else {
                        // 等待更多数据
                        auto now = std::chrono::steady_clock::now();
                        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();
                        
                        if (elapsed > 20) { // 20秒无数据则超时
                            LOG_WARN << "接收数据超时";
                            break;
                        }
                        
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    }
                }
            }
            
            // 详细记录接收到的数据
            LOG_INFO << "接收到原始响应，长度: " << response.length();
            if (!response.empty()) {
                LOG_INFO << "响应前100字符: " << response.substr(0, 100);
                LOG_INFO << "响应最后100字符: " << (response.length() > 100 ? 
                          response.substr(response.length() - 100) : response);
            }
            
            // 尝试解析JSON
            try {
                // 处理可能的编码问题
                cleanInvalidUtf8(response);
                
                json parsed = json::parse(response);
                if (parsed.contains("response")) {
                    std::string ai_response = parsed["response"].get<std::string>();
                    LOG_INFO << "成功解析JSON，获取到response字段";
                    return ai_response;
                } else {
                    LOG_ERROR << "JSON中缺少response字段";
                    return "响应格式错误，请重试";
                }
            } catch (const json::parse_error& e) {
                LOG_ERROR << "JSON解析失败: " << e.what();
                
                // 失败后尝试正则表达式提取
                std::regex responseRegex("\"response\"\\s*:\\s*\"(.*?)\"\\s*,");
                std::smatch match;
                if (std::regex_search(response, match, responseRegex) && match.size() > 1) {
                    LOG_INFO << "通过正则提取到response内容";
                    std::string extracted = match[1].str();
                    // 处理转义字符
                    replaceAll(extracted, "\\n", "\n");
                    replaceAll(extracted, "\\\"", "\"");
                    replaceAll(extracted, "\\\\", "\\");
                    return extracted;
                }
                
                // 如果完全无法解析，返回可能有用的内容
                if (response.length() > 20) {
                    LOG_INFO << "返回未解析的响应内容";
                    return "解析错误，原始内容：" + response.substr(0, 1000);
                }
                return "无法解析服务器响应，请重试";
            }
        } catch (const std::exception& e) {
            LOG_ERROR << "处理请求异常: " << e.what();
            return "处理请求时发生错误，请重试";
        }
    }
    
    void resetConnection() {
        LOG_INFO << "重置与LLaMA服务的连接";
        closeConnection();  // 关闭现有连接
        available_ = checkServiceAvailable();  // 重新检查可用性
        if (available_) {
            LOG_INFO << "LLaMA 服务重新连接成功";
        } else {
            LOG_ERROR << "LLaMA 服务重新连接失败";
        }
    }
    
private:
    bool checkServiceAvailable() {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            LOG_ERROR << "创建套接字失败";
            return false;
        }
        
        // 设置更短的连接超时，避免长时间等待
        struct timeval tv;
        tv.tv_sec = 1;  // 1秒超时
        tv.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof tv);
        
        // 连接服务器
        struct sockaddr_in serv_addr;
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(serverPort_);
        inet_pton(AF_INET, serverIp_.c_str(), &serv_addr.sin_addr);
        
        bool success = connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) >= 0;
        if (!success) {
            LOG_ERROR << "LLaMA 服务连接失败: " << strerror(errno);
        }
        close(sock);
        return success;
    }

    bool ensureConnection() {
        if (persistent_sock_ > 0) {
            // 测试连接是否还活着
            char test_buf[1];
            int result = recv(persistent_sock_, test_buf, 1, MSG_PEEK | MSG_DONTWAIT);
            if (result >= 0 || (result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))) {
                return true; // 连接正常
            }
            
            LOG_INFO << "连接已断开，尝试重新连接";
            closeConnection();
        }
        
        // 创建新连接
        persistent_sock_ = socket(AF_INET, SOCK_STREAM, 0);
        if (persistent_sock_ < 0) {
            LOG_ERROR << "无法创建套接字";
            return false;
        }
        
        // 设置超时
        struct timeval tv;
        tv.tv_sec = 30;
        tv.tv_usec = 0;
        setsockopt(persistent_sock_, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
        setsockopt(persistent_sock_, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof tv);
        
        // 连接服务器
        struct sockaddr_in serv_addr;
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(serverPort_);
        inet_pton(AF_INET, serverIp_.c_str(), &serv_addr.sin_addr);
        
        if (connect(persistent_sock_, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
            LOG_ERROR << "连接到LLaMA服务器失败: " << strerror(errno);
            closeConnection();
            available_ = false;
            return false;
        }
        
        // 设置非阻塞模式
        int flags = fcntl(persistent_sock_, F_GETFL, 0);
        fcntl(persistent_sock_, F_SETFL, flags | O_NONBLOCK);
        
        available_ = true;
        LOG_INFO << "成功建立到LLaMA服务的新连接";
        return true;
    }
    
    void closeConnection() {
        if (persistent_sock_ > 0) {
            close(persistent_sock_);
            persistent_sock_ = -1;
        }
    }

    std::string modelPath_;
    std::string serverIp_;
    int serverPort_;
    bool available_;
    int persistent_sock_ = -1; // 持久连接套接字
};

class LlamaHttpService {
public:
    LlamaHttpService(EventLoop* loop, const InetAddress& addr) 
        : httpServer_(loop, addr, "LlamaHttpServer")
        , llama_service_("/home/shl203/llama.cpp/models/qwen/Qwen-7B-Chat.Q4_K_M.gguf")
        , lfu_cache_(100)
    {
        // 初始化数据库
        kama::db::DbConfig dbConfig;
        dbConfig.host = "localhost";
        dbConfig.port = 3306;
        dbConfig.dbname = "kama_llm";
        dbConfig.user = "root";      // 请根据实际情况修改
        dbConfig.password = "password"; // 请根据实际情况修改
        dbConfig.charset = "utf8mb4";
        
        // 先测试数据库连接
        LOG_INFO << "尝试连接数据库: " << dbConfig.host << ":" << dbConfig.port << "/" << dbConfig.dbname;
        LOG_INFO << "使用用户: " << dbConfig.user;
        
        if (!kama::db::DatabaseManager::instance().initialize(dbConfig)) {
            LOG_ERROR << "数据库初始化失败！将继续运行但数据库功能不可用";
            // 不要让程序退出，继续运行但标记数据库不可用
        } else {
            LOG_INFO << "数据库初始化成功，连接到 " << dbConfig.host << ":" << dbConfig.port;
        }
        
        setupRoutes();
        httpServer_.setThreadNum(4);
    }
    
    void start() {
        httpServer_.start();
    }

private:
    void setupRoutes() {
        // 主页面
        httpServer_.get("/", [this](const HttpRequest& req, HttpResponse& resp) {
            resp.setHtmlResponse(getWebInterface());
        });
        
        // API 路由
        httpServer_.post("/api/chat", [this](const HttpRequest& req, HttpResponse& resp) {
            handleChatRequest(req, resp);
        });
        
        httpServer_.get("/api/status", [this](const HttpRequest& req, HttpResponse& resp) {
            handleStatusRequest(req, resp);
        });
        
        // 新增数据库相关路由
        httpServer_.get("/api/history", [this](const HttpRequest& req, HttpResponse& resp) {
            handleHistoryRequest(req, resp);
        });
        
        httpServer_.post("/api/login", [this](const HttpRequest& req, HttpResponse& resp) {
            handleLoginRequest(req, resp);
        });
        
        httpServer_.post("/api/register", [this](const HttpRequest& req, HttpResponse& resp) {
            handleRegisterRequest(req, resp);
        });
        
        httpServer_.get("/api/sessions", [this](const HttpRequest& req, HttpResponse& resp) {
            handleSessionsRequest(req, resp);
        });
        
        // 静态文件支持
        httpServer_.setStaticFileRoot("./static");
        httpServer_.enableStaticFiles(true);
    }
    
    void handleChatRequest(const HttpRequest& req, HttpResponse& resp) {
        try {
            // 解析JSON请求
            std::string message;
            std::string sessionId = "default"; // 默认会话ID
            std::string userId = "anonymous";  // 默认用户ID
            
            try {
                json reqJson = json::parse(req.body());
                if (reqJson.contains("message")) {
                    message = reqJson["message"].get<std::string>();
                }
                
                // 获取会话ID
                if (reqJson.contains("session_id")) {
                    sessionId = reqJson["session_id"].get<std::string>();
                }
                
                // 获取用户ID
                if (reqJson.contains("user_id")) {
                    userId = reqJson["user_id"].get<std::string>();
                }
                
            } catch (const json::parse_error& e) {
                LOG_ERROR << "解析请求JSON失败: " << e.what();
            }
            
            // 如果JSON解析失败，尝试正则表达式提取
            if (message.empty()) {
                message = parseJsonMessage(req.body());
            }
            
            // 增强的消息验证
            if (message.empty()) {
                LOG_ERROR << "收到空消息请求，原始body: " << req.body();
                resp.setStatusCode(HttpStatusCode::BAD_REQUEST);
                resp.setStatusMessage("Bad Request");
                resp.setJsonResponse(buildJsonResponse("请求格式错误，缺少message字段", false));
                return;
            }
            
            // 进一步验证消息内容
            std::string trimmedMessage = message;
            // 移除前后空白字符
            trimmedMessage.erase(0, trimmedMessage.find_first_not_of(" \t\n\r"));
            trimmedMessage.erase(trimmedMessage.find_last_not_of(" \t\n\r") + 1);
            
            if (trimmedMessage.empty()) {
                LOG_ERROR << "收到空白消息请求: '" << message << "'";
                resp.setStatusCode(HttpStatusCode::BAD_REQUEST);
                resp.setStatusMessage("Bad Request");
                resp.setJsonResponse(buildJsonResponse("消息内容不能为空", false));
                return;
            }
            
            // 使用清理后的消息
            message = trimmedMessage;
            LOG_INFO << "收到聊天请求: '" << message << "'";
            
            // 确保会话存在（仅在数据库可用时）
            if (kama::db::DatabaseManager::instance().isInitialized()) {
                if (sessionId == "default" || sessionId.empty()) {
                    sessionId = kama::db::DatabaseManager::instance().createSession(userId, "默认会话");
                    if (!sessionId.empty()) {
                        LOG_INFO << "创建新会话: " << sessionId << " 用户: " << userId;
                    } else {
                        sessionId = "session_" + generateUUID(); // 使用临时会话ID
                        LOG_WARN << "数据库创建会话失败，使用临时会话ID: " << sessionId;
                    }
                } else {
                    // 更新会话活动时间
                    kama::db::DatabaseManager::instance().updateSessionActivity(sessionId);
                }
                
                // 将用户消息保存到数据库
                if (kama::db::DatabaseManager::instance().saveConversation(
                    sessionId, "user", message)) {
                    LOG_INFO << "用户消息已保存到数据库";
                } else {
                    LOG_WARN << "保存用户消息到数据库失败";
                }
            } else {
                LOG_WARN << "数据库不可用，跳过会话和消息持久化";
                if (sessionId == "default" || sessionId.empty()) {
                    sessionId = "session_" + generateUUID();
                }
            }
            
            // 首先从数据库缓存检查（仅在数据库可用时）
            std::string cachedResult;
            if (kama::db::DatabaseManager::instance().isInitialized()) {
                cachedResult = kama::db::DatabaseManager::instance().getResponseFromCache(message);
            }
            
            if (!cachedResult.empty()) {
                LOG_INFO << "数据库缓存命中";
                
                // 更新缓存统计（仅在数据库可用时）
                if (kama::db::DatabaseManager::instance().isInitialized()) {
                    kama::db::DatabaseManager::instance().updateCacheStats(message);
                    
                    // 将AI响应保存到数据库
                    kama::db::DatabaseManager::instance().saveConversation(
                        sessionId, "ai", cachedResult);
                }
                
                // 构建JSON响应，包含会话信息
                json respJson;
                respJson["response"] = cachedResult;
                respJson["cached"] = true;
                respJson["session_id"] = sessionId;
                resp.setJsonResponse(respJson.dump());
                return;
            }
            
            // 然后检查内存缓存
            std::string memCachedResult;
            if (lfu_cache_.get(message, memCachedResult)) {
                LOG_INFO << "内存缓存命中";
                
                // 同步到数据库缓存
                kama::db::DatabaseManager::instance().saveToCache(message, memCachedResult);
                
                // 将AI响应保存到数据库
                kama::db::DatabaseManager::instance().saveConversation(
                    sessionId, "ai", memCachedResult);
                
                // 构建JSON响应，包含会话信息
                json respJson;
                respJson["response"] = memCachedResult;
                respJson["cached"] = true;
                respJson["session_id"] = sessionId;
                resp.setJsonResponse(respJson.dump());
                return;
            }
            
            // 检查服务可用性并增加重试机制
            int retryCount = 0;
            std::string result;
            bool success = false;
            
            while (retryCount < 3 && !success) {
                if (!llama_service_.isServiceAvailable()) {
                    if (retryCount == 0) {
                        LOG_WARN << "LLaMA 服务不可用，尝试重新连接";
                        // 尝试恢复服务
                        llama_service_.resetConnection();
                        std::this_thread::sleep_for(std::chrono::seconds(1));
                    } else {
                        break; // 多次尝试后仍然不可用
                    }
                }
                
                try {
                    // 获取会话历史记录，用于上下文感知回复
                    std::vector<kama::db::ConversationRecord> history = 
                        kama::db::DatabaseManager::instance().getConversationHistory(sessionId, 10);
                    
                    // 构建带历史记录的提示
                    std::string fullPrompt;
                    if (!history.empty()) {
                        // 构建会话历史上下文
                        std::stringstream contextBuilder;
                        for (const auto& record : history) {
                            if (record.message_type == "user") {
                                contextBuilder << "用户: " << record.content << "\n";
                            } else {
                                contextBuilder << "AI: " << record.content << "\n";
                            }
                        }
                        contextBuilder << "用户: " << message << "\nAI:";
                        fullPrompt = contextBuilder.str();
                    } else {
                        fullPrompt = message;
                    }
                    
                    result = llama_service_.query(fullPrompt);
                    if (!result.empty() && result != "无法解析服务器响应，请重试" && 
                        result != "服务暂时不可用，请稍后重试") {
                        success = true;
                    }
                } catch (const std::exception& e) {
                    LOG_ERROR << "查询LLaMA服务异常: " << e.what();
                }
                
                retryCount++;
                if (!success && retryCount < 3) {
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }
            }
            
            if (success) {
                // 保存到内存缓存
                lfu_cache_.put(message, result);
                
                // 保存到数据库缓存
                kama::db::DatabaseManager::instance().saveToCache(message, result);
                
                // 将AI响应保存到数据库
                kama::db::DatabaseManager::instance().saveConversation(
                    sessionId, "ai", result);
                
                // 构建JSON响应，包含会话信息
                json respJson;
                respJson["response"] = result;
                respJson["cached"] = false;
                respJson["session_id"] = sessionId;
                resp.setJsonResponse(respJson.dump());
            } else {
                // 所有尝试均失败
                LOG_ERROR << "无法获取LLaMA响应，请求: " << message;
                resp.setStatusCode(HttpStatusCode::SERVICE_UNAVAILABLE);
                resp.setStatusMessage("Service Unavailable");
                
                json errorJson;
                errorJson["error"] = "LLaMA服务暂时不可用，请稍后重试";
                errorJson["session_id"] = sessionId;
                resp.setJsonResponse(errorJson.dump());
            }
        } catch (const std::exception& e) {
            LOG_ERROR << "处理聊天请求异常: " << e.what();
            resp.setStatusCode(HttpStatusCode::INTERNAL_SERVER_ERROR);
            resp.setStatusMessage("Internal Server Error");
            resp.setJsonResponse(buildJsonResponse("服务器内部错误", false));
        }
    }
    
    void handleStatusRequest(const HttpRequest& req, HttpResponse& resp) {
        // 扩展状态信息，包含数据库状态
        json statusJson;
        statusJson["cache_size"] = static_cast<int>(lfu_cache_.size());
        statusJson["llama_available"] = llama_service_.isServiceAvailable();
        statusJson["database_available"] = kama::db::DatabaseManager::instance().isInitialized();
        statusJson["timestamp"] = kama::db::DatabaseManager::instance().getCurrentTimestamp();
        
        // 添加数据库统计信息
        if (kama::db::DatabaseManager::instance().isInitialized()) {
            statusJson["sessions_count"] = kama::db::DatabaseManager::instance().getSessionsCount();
            statusJson["users_count"] = kama::db::DatabaseManager::instance().getUsersCount();
            statusJson["conversations_count"] = kama::db::DatabaseManager::instance().getConversationsCount();
            statusJson["cache_hits"] = kama::db::DatabaseManager::instance().getCacheHits();
        }
        
        resp.setJsonResponse(statusJson.dump());
    }
    
    // 处理会话历史请求
    void handleHistoryRequest(const HttpRequest& req, HttpResponse& resp) {
        std::string sessionId;
        int limit = 20; // 默认限制
        
        // 从URL参数中获取会话ID和限制
        std::string query = req.query();
        std::regex sessionRegex("session_id=([^&]+)");
        std::regex limitRegex("limit=(\\d+)");
        
        std::smatch match;
        if (std::regex_search(query, match, sessionRegex)) {
            sessionId = match[1];
        }
        
        if (std::regex_search(query, match, limitRegex)) {
            limit = std::stoi(match[1]);
        }
        
        if (sessionId.empty()) {
            resp.setStatusCode(HttpStatusCode::BAD_REQUEST);
            resp.setStatusMessage("Bad Request");
            resp.setJsonResponse("{\"error\":\"缺少会话ID参数\"}");
            return;
        }
        
        try {
            // 获取会话历史记录
            std::vector<kama::db::ConversationRecord> history = 
                kama::db::DatabaseManager::instance().getConversationHistory(sessionId, limit);
            
            // 构建JSON响应
            json historyJson;
            historyJson["session_id"] = sessionId;
            
            json messagesJson = json::array();
            for (const auto& record : history) {
                json messageJson;
                messageJson["id"] = record.message_id;
                messageJson["type"] = record.message_type;
                messageJson["content"] = record.content;
                messageJson["timestamp"] = record.timestamp;
                messagesJson.push_back(messageJson);
            }
            
            historyJson["messages"] = messagesJson;
            resp.setJsonResponse(historyJson.dump());
            
        } catch (const std::exception& e) {
            LOG_ERROR << "获取会话历史异常: " << e.what();
            resp.setStatusCode(HttpStatusCode::INTERNAL_SERVER_ERROR);
            resp.setStatusMessage("Internal Server Error");
            resp.setJsonResponse("{\"error\":\"获取历史记录失败\"}");
        }
    }
    
    // 处理用户登录请求
    void handleLoginRequest(const HttpRequest& req, HttpResponse& resp) {
        try {
            json reqJson = json::parse(req.body());
            std::string username, password;
            
            if (reqJson.contains("username") && reqJson.contains("password")) {
                username = reqJson["username"].get<std::string>();
                password = reqJson["password"].get<std::string>();
            } else {
                resp.setStatusCode(HttpStatusCode::BAD_REQUEST);
                resp.setStatusMessage("Bad Request");
                resp.setJsonResponse("{\"error\":\"缺少用户名或密码\"}");
                return;
            }
            
            // 验证用户凭据
            if (kama::db::DatabaseManager::instance().authenticateUser(username, password)) {
                // 更新登录时间
                kama::db::DatabaseManager::instance().updateUserLastLogin(username);
                
                // 获取用户信息
                kama::db::UserInfo userInfo = 
                    kama::db::DatabaseManager::instance().getUserInfo(username);
                
                // 创建新会话
                std::string sessionId = 
                    kama::db::DatabaseManager::instance().createSession(userInfo.user_id, "登录会话");
                
                // 构建响应
                json loginJson;
                loginJson["success"] = true;
                loginJson["user_id"] = userInfo.user_id;
                loginJson["username"] = username;
                loginJson["session_id"] = sessionId;
                resp.setJsonResponse(loginJson.dump());
            } else {
                resp.setStatusCode(HttpStatusCode::UNAUTHORIZED);
                resp.setStatusMessage("Unauthorized");
                resp.setJsonResponse("{\"error\":\"用户名或密码错误\"}");
            }
            
        } catch (const std::exception& e) {
            LOG_ERROR << "处理登录请求异常: " << e.what();
            resp.setStatusCode(HttpStatusCode::INTERNAL_SERVER_ERROR);
            resp.setStatusMessage("Internal Server Error");
            resp.setJsonResponse("{\"error\":\"登录处理失败\"}");
        }
    }
    
    // 处理用户注册请求
    void handleRegisterRequest(const HttpRequest& req, HttpResponse& resp) {
        try {
            json reqJson = json::parse(req.body());
            std::string username, password, email;
            
            if (reqJson.contains("username") && reqJson.contains("password")) {
                username = reqJson["username"].get<std::string>();
                password = reqJson["password"].get<std::string>();
                
                if (reqJson.contains("email")) {
                    email = reqJson["email"].get<std::string>();
                }
            } else {
                resp.setStatusCode(HttpStatusCode::BAD_REQUEST);
                resp.setStatusMessage("Bad Request");
                resp.setJsonResponse("{\"error\":\"缺少必要的注册信息\"}");
                return;
            }
            
            // 创建用户
            if (kama::db::DatabaseManager::instance().createUser(username, password, email)) {
                // 获取用户信息
                kama::db::UserInfo userInfo = 
                    kama::db::DatabaseManager::instance().getUserInfo(username);
                
                // 创建新会话
                std::string sessionId = 
                    kama::db::DatabaseManager::instance().createSession(userInfo.user_id, "初始会话");
                
                // 构建响应
                json registerJson;
                registerJson["success"] = true;
                registerJson["user_id"] = userInfo.user_id;
                registerJson["username"] = username;
                registerJson["session_id"] = sessionId;
                resp.setJsonResponse(registerJson.dump());
            } else {
                resp.setStatusCode(HttpStatusCode::CONFLICT);
                resp.setStatusMessage("Conflict");
                resp.setJsonResponse("{\"error\":\"用户名已存在\"}");
            }
            
        } catch (const std::exception& e) {
            LOG_ERROR << "处理注册请求异常: " << e.what();
            resp.setStatusCode(HttpStatusCode::INTERNAL_SERVER_ERROR);
            resp.setStatusMessage("Internal Server Error");
            resp.setJsonResponse("{\"error\":\"注册处理失败\"}");
        }
    }
    
    // 处理会话列表请求
    void handleSessionsRequest(const HttpRequest& req, HttpResponse& resp) {
        std::string userId;
        
        // 从URL参数中获取用户ID
        std::string query = req.query();
        std::regex userIdRegex("user_id=([^&]+)");
        
        std::smatch match;
        if (std::regex_search(query, match, userIdRegex)) {
            userId = match[1];
        }
        
        if (userId.empty()) {
            resp.setStatusCode(HttpStatusCode::BAD_REQUEST);
            resp.setStatusMessage("Bad Request");
            resp.setJsonResponse("{\"error\":\"缺少用户ID参数\"}");
            return;
        }
        
        try {
            // 获取用户会话列表
            std::vector<kama::db::SessionInfo> sessions = 
                kama::db::DatabaseManager::instance().getUserSessions(userId);
            
            // 构建JSON响应
            json sessionsJson;
            sessionsJson["user_id"] = userId;
            
            json sessionArray = json::array();
            for (const auto& session : sessions) {
                json sessionJson;
                sessionJson["id"] = session.session_id;
                sessionJson["name"] = session.session_name;
                sessionJson["created_at"] = session.created_at;
                sessionJson["last_active"] = session.last_active;
                sessionArray.push_back(sessionJson);
            }
            
            sessionsJson["sessions"] = sessionArray;
            resp.setJsonResponse(sessionsJson.dump());
            
        } catch (const std::exception& e) {
            LOG_ERROR << "获取会话列表异常: " << e.what();
            resp.setStatusCode(HttpStatusCode::INTERNAL_SERVER_ERROR);
            resp.setStatusMessage("Internal Server Error");
            resp.setJsonResponse("{\"error\":\"获取会话列表失败\"}");
        }
    }
    
    std::string getWebInterface() {
        std::string html;
        
        // HTML 头部
        html += "<!DOCTYPE html>\n";
        html += "<html lang=\"zh-CN\">\n";
        html += "<head>\n";
        html += "    <meta charset=\"UTF-8\">\n";
        html += "    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n";
        html += "    <title>CSUshl203 Chat</title>\n";
        html += "    <style>\n";
        html += "        body { font-family: Arial, sans-serif; margin: 0; padding: 20px; background-color: #f5f5f5; }\n";
        html += "        .container { max-width: 800px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }\n";
        html += "        .chat-box { border: 1px solid #ddd; height: 400px; overflow-y: auto; padding: 10px; margin-bottom: 20px; background-color: #fafafa; }\n";
        html += "        .message { margin-bottom: 10px; padding: 8px; border-radius: 5px; }\n";
        html += "        .user-message { background-color: #007bff; color: white; text-align: right; }\n";
        html += "        .bot-message { background-color: #e9ecef; color: #333; }\n";
        html += "        .input-area { display: flex; gap: 10px; margin-bottom: 10px; }\n";
        html += "        #messageInput { flex: 1; padding: 10px; border: 1px solid #ddd; border-radius: 5px; }\n";
        html += "        #sendButton { padding: 10px 20px; background-color: #007bff; color: white; border: none; border-radius: 5px; cursor: pointer; }\n";
        html += "        #sendButton:hover { background-color: #0056b3; }\n";
        html += "        #sendButton:disabled { background-color: #ccc; cursor: not-allowed; }\n";
        html += "        .status { margin-top: 10px; font-size: 12px; color: #666; }\n";
        html += "        .user-info { display: flex; justify-content: space-between; margin-bottom: 10px; }\n";
        html += "        .session-info { font-size: 13px; color: #666; }\n";
        html += "        .user-menu { display: flex; gap: 10px; }\n";
        html += "        .btn { padding: 5px 10px; background-color: #6c757d; color: white; border: none; border-radius: 3px; cursor: pointer; font-size: 12px; }\n";
        html += "        .login-form { display: none; padding: 20px; border: 1px solid #ddd; border-radius: 5px; margin-bottom: 20px; }\n";
        html += "        .form-group { margin-bottom: 15px; }\n";
        html += "        .form-group label { display: block; margin-bottom: 5px; }\n";
        html += "        .form-group input { width: 100%; padding: 8px; border: 1px solid #ddd; border-radius: 3px; }\n";
        html += "    </style>\n";
        html += "</head>\n";
        html += "<body>\n";
        html += "    <div class=\"container\">\n";
        html += "        <h1>CSUshl203 Chat Interface</h1>\n";
        
        // 用户信息和登录表单
        html += "        <div class=\"user-info\">\n";
        html += "            <div class=\"session-info\" id=\"sessionInfo\">未登录</div>\n";
        html += "            <div class=\"user-menu\">\n";
        html += "                <button id=\"loginBtn\" class=\"btn\" onclick=\"toggleLoginForm()\">登录</button>\n";
        html += "                <button id=\"registerBtn\" class=\"btn\" onclick=\"toggleRegisterForm()\">注册</button>\n";
        html += "                <button id=\"logoutBtn\" class=\"btn\" onclick=\"logout()\" style=\"display:none;\">退出</button>\n";
        html += "            </div>\n";
        html += "        </div>\n";
        
        // 登录表单
        html += "        <div id=\"loginForm\" class=\"login-form\">\n";
        html += "            <div class=\"form-group\">\n";
        html += "                <label for=\"loginUsername\">用户名</label>\n";
        html += "                <input type=\"text\" id=\"loginUsername\" required>\n";
        html += "            </div>\n";
        html += "            <div class=\"form-group\">\n";
        html += "                <label for=\"loginPassword\">密码</label>\n";
        html += "                <input type=\"password\" id=\"loginPassword\" required>\n";
        html += "            </div>\n";
        html += "            <button onclick=\"login()\" class=\"btn\" style=\"background-color:#28a745;\">登录</button>\n";
        html += "            <button onclick=\"toggleLoginForm()\" class=\"btn\">取消</button>\n";
        html += "        </div>\n";
        
        // 注册表单
        html += "        <div id=\"registerForm\" class=\"login-form\">\n";
        html += "            <div class=\"form-group\">\n";
        html += "                <label for=\"regUsername\">用户名</label>\n";
        html += "                <input type=\"text\" id=\"regUsername\" required>\n";
        html += "            </div>\n";
        html += "            <div class=\"form-group\">\n";
        html += "                <label for=\"regPassword\">密码</label>\n";
        html += "                <input type=\"password\" id=\"regPassword\" required>\n";
        html += "            </div>\n";
        html += "            <div class=\"form-group\">\n";
        html += "                <label for=\"regEmail\">邮箱</label>\n";
        html += "                <input type=\"email\" id=\"regEmail\">\n";
        html += "            </div>\n";
        html += "            <button onclick=\"register()\" class=\"btn\" style=\"background-color:#28a745;\">注册</button>\n";
        html += "            <button onclick=\"toggleRegisterForm()\" class=\"btn\">取消</button>\n";
        html += "        </div>\n";
        
        // 会话选择器
        html += "        <div id=\"sessionSelector\" style=\"display:none; margin-bottom:10px;\">\n";
        html += "            <label for=\"sessionSelect\">选择会话: </label>\n";
        html += "            <select id=\"sessionSelect\" onchange=\"changeSession(this.value)\">\n";
        html += "                <option value=\"\">加载中...</option>\n";
        html += "            </select>\n";
        html += "            <button onclick=\"createNewSession()\" class=\"btn\">新建会话</button>\n";
        html += "        </div>\n";
        
        // 聊天框和输入区
        html += "        <div id=\"chatBox\" class=\"chat-box\"></div>\n";
        html += "        <div class=\"input-area\">\n";
        html += "            <input type=\"text\" id=\"messageInput\" placeholder=\"输入您的问题...\" />\n";
        html += "            <button id=\"sendButton\" onclick=\"sendMessage()\">发送</button>\n";
        html += "        </div>\n";
        html += "        <div class=\"status\" id=\"status\">准备就绪</div>\n";
        html += "    </div>\n";
        
        // JavaScript 部分
        html += "    <script>\n";
        html += "        // 用户和会话数据\n";
        html += "        let currentUser = {\n";
        html += "            userId: null,\n";
        html += "            username: null,\n";
        html += "            sessionId: null\n";
        html += "        };\n";
        html += "\n";
        html += "        // 初始化：检查本地存储中的会话信息\n";
        html += "        function initializeApp() {\n";
        html += "            const storedUser = localStorage.getItem('currentUser');\n";
        html += "            if (storedUser) {\n";
        html += "                try {\n";
        html += "                    currentUser = JSON.parse(storedUser);\n";
        html += "                    updateUIForLoggedInUser();\n";
        html += "                    loadUserSessions();\n";
        html += "                    if (currentUser.sessionId) {\n";
        html += "                        loadSessionHistory(currentUser.sessionId);\n";
        html += "                    }\n";
        html += "                } catch (e) {\n";
        html += "                    console.error('无法解析存储的用户数据');\n";
        html += "                    localStorage.removeItem('currentUser');\n";
        html += "                }\n";
        html += "            }\n";
        html += "        }\n";
        html += "\n";
        html += "        // 更新UI以反映已登录状态\n";
        html += "        function updateUIForLoggedInUser() {\n";
        html += "            document.getElementById('loginBtn').style.display = 'none';\n";
        html += "            document.getElementById('registerBtn').style.display = 'none';\n";
        html += "            document.getElementById('logoutBtn').style.display = 'inline-block';\n";
        html += "            document.getElementById('sessionInfo').textContent = `用户: ${currentUser.username} | 会话ID: ${currentUser.sessionId || '未选择'}`;\n";
        html += "            document.getElementById('sessionSelector').style.display = 'block';\n";
        html += "        }\n";
        html += "\n";
        html += "        // 加载用户的会话列表\n";
        html += "        function loadUserSessions() {\n";
        html += "            if (!currentUser.userId) return;\n";
        html += "            \n";
        html += "            fetch(`/api/sessions?user_id=${currentUser.userId}`)\n";
        html += "                .then(response => response.json())\n";
        html += "                .then(data => {\n";
        html += "                    const sessionSelect = document.getElementById('sessionSelect');\n";
        html += "                    sessionSelect.innerHTML = '';\n";
        html += "                    \n";
        html += "                    if (data.sessions && Array.isArray(data.sessions)) {\n";
        html += "                        data.sessions.forEach(session => {\n";
        html += "                            const option = document.createElement('option');\n";
        html += "                            option.value = session.id;\n";
        html += "                            option.textContent = session.name || `会话 ${session.id.substr(0, 8)}`;\n";
        html += "                            sessionSelect.appendChild(option);\n";
        html += "                            \n";
        html += "                            if (session.id === currentUser.sessionId) {\n";
        html += "                                option.selected = true;\n";
        html += "                            }\n";
        html += "                        });\n";
        html += "                    } else {\n";
        html += "                        const option = document.createElement('option');\n";
        html += "                        option.value = '';\n";
        html += "                        option.textContent = '无可用会话';\n";
        html += "                        sessionSelect.appendChild(option);\n";
        html += "                    }\n";
        html += "                })\n";
        html += "                .catch(error => {\n";
        html += "                    console.error('加载会话失败:', error);\n";
        html += "                });\n";
        html += "        }\n";
        html += "\n";
        html += "        // 加载会话历史\n";
        html += "        function loadSessionHistory(sessionId) {\n";
        html += "            if (!sessionId) return;\n";
        html += "            \n";
        html += "            document.getElementById('chatBox').innerHTML = '';\n";
        html += "            document.getElementById('status').textContent = '加载历史记录...';\n";
        html += "            \n";
        html += "            fetch(`/api/history?session_id=${sessionId}&limit=30`)\n";
        html += "                .then(response => response.json())\n";
        html += "                .then(data => {\n";
        html += "                    if (data.messages && Array.isArray(data.messages)) {\n";
        html += "                        data.messages.forEach(msg => {\n";
        html += "                            addMessage(msg.content, msg.type === 'user');\n";
        html += "                        });\n";
        html += "                        document.getElementById('status').textContent = `已加载 ${data.messages.length} 条历史消息`;\n";
        html += "                    } else {\n";
        html += "                        document.getElementById('status').textContent = '没有历史记录';\n";
        html += "                    }\n";
        html += "                })\n";
        html += "                .catch(error => {\n";
        html += "                    console.error('加载历史记录失败:', error);\n";
        html += "                    document.getElementById('status').textContent = '加载历史记录失败';\n";
        html += "                });\n";
        html += "        }\n";
        html += "\n";
        html += "        // 切换登录表单显示\n";
        html += "        function toggleLoginForm() {\n";
        html += "            const loginForm = document.getElementById('loginForm');\n";
        html += "            const registerForm = document.getElementById('registerForm');\n";
        html += "            registerForm.style.display = 'none';\n";
        html += "            loginForm.style.display = loginForm.style.display === 'block' ? 'none' : 'block';\n";
        html += "        }\n";
        html += "\n";
        html += "        // 切换注册表单显示\n";
        html += "        function toggleRegisterForm() {\n";
        html += "            const loginForm = document.getElementById('loginForm');\n";
        html += "            const registerForm = document.getElementById('registerForm');\n";
        html += "            loginForm.style.display = 'none';\n";
        html += "            registerForm.style.display = registerForm.style.display === 'block' ? 'none' : 'block';\n";
        html += "        }\n";
        html += "\n";
        html += "        // 用户登录\n";
        html += "        function login() {\n";
        html += "            const username = document.getElementById('loginUsername').value.trim();\n";
        html += "            const password = document.getElementById('loginPassword').value;\n";
        html += "            \n";
        html += "            if (!username || !password) {\n";
        html += "                alert('请输入用户名和密码');\n";
        html += "                return;\n";
        html += "            }\n";
        html += "            \n";
        html += "            document.getElementById('status').textContent = '登录中...';\n";
        html += "            \n";
        html += "            fetch('/api/login', {\n";
        html += "                method: 'POST',\n";
        html += "                headers: { 'Content-Type': 'application/json' },\n";
        html += "                body: JSON.stringify({ username, password })\n";
        html += "            })\n";
        html += "            .then(response => response.json())\n";
        html += "            .then(data => {\n";
        html += "                if (data.success) {\n";
        html += "                    currentUser = {\n";
        html += "                        userId: data.user_id,\n";
        html += "                        username: data.username,\n";
        html += "                        sessionId: data.session_id\n";
        html += "                    };\n";
        html += "                    \n";
        html += "                    // 保存用户信息到本地存储\n";
        html += "                    localStorage.setItem('currentUser', JSON.stringify(currentUser));\n";
        html += "                    \n";
        html += "                    // 更新UI\n";
        html += "                    updateUIForLoggedInUser();\n";
        html += "                    toggleLoginForm();\n";
        html += "                    loadUserSessions();\n";
        html += "                    loadSessionHistory(currentUser.sessionId);\n";
        html += "                    document.getElementById('status').textContent = '登录成功';\n";
        html += "                } else {\n";
        html += "                    document.getElementById('status').textContent = data.error || '登录失败';\n";
        html += "                }\n";
        html += "            })\n";
        html += "            .catch(error => {\n";
        html += "                console.error('登录失败:', error);\n";
        html += "                document.getElementById('status').textContent = '登录失败';\n";
        html += "            });\n";
        html += "        }\n";
        html += "\n";
        html += "        // 用户注册\n";
        html += "        function register() {\n";
        html += "            const username = document.getElementById('regUsername').value.trim();\n";
        html += "            const password = document.getElementById('regPassword').value;\n";
        html += "            const email = document.getElementById('regEmail').value.trim();\n";
        html += "            \n";
        html += "            if (!username || !password) {\n";
        html += "                alert('请输入用户名和密码');\n";
        html += "                return;\n";
        html += "            }\n";
        html += "            \n";
        html += "            document.getElementById('status').textContent = '注册中...';\n";
        html += "            \n";
        html += "            fetch('/api/register', {\n";
        html += "                method: 'POST',\n";
        html += "                headers: { 'Content-Type': 'application/json' },\n";
        html += "                body: JSON.stringify({ username, password, email })\n";
        html += "            })\n";
        html += "            .then(response => response.json())\n";
        html += "            .then(data => {\n";
        html += "                if (data.success) {\n";
        html += "                    currentUser = {\n";
        html += "                        userId: data.user_id,\n";
        html += "                        username: data.username,\n";
        html += "                        sessionId: data.session_id\n";
        html += "                    };\n";
        html += "                    \n";
        html += "                    // 保存用户信息到本地存储\n";
        html += "                    localStorage.setItem('currentUser', JSON.stringify(currentUser));\n";
        html += "                    \n";
        html += "                    // 更新UI\n";
        html += "                    updateUIForLoggedInUser();\n";
        html += "                    toggleRegisterForm();\n";
        html += "                    loadUserSessions();\n";
        html += "                    document.getElementById('chatBox').innerHTML = '';\n";
        html += "                    document.getElementById('status').textContent = '注册成功';\n";
        html += "                } else {\n";
        html += "                    document.getElementById('status').textContent = data.error || '注册失败';\n";
        html += "                }\n";
        html += "            })\n";
        html += "            .catch(error => {\n";
        html += "                console.error('注册失败:', error);\n";
        html += "                document.getElementById('status').textContent = '注册失败';\n";
        html += "            });\n";
        html += "        }\n";
        html += "\n";
        html += "        // 退出登录\n";
        html += "        function logout() {\n";
        html += "            currentUser = {\n";
        html += "                userId: null,\n";
        html += "                username: null,\n";
        html += "                sessionId: null\n";
        html += "            };\n";
        html += "            \n";
        html += "            // 清除本地存储\n";
        html += "            localStorage.removeItem('currentUser');\n";
        html += "            \n";
        html += "            // 更新UI\n";
        html += "            document.getElementById('loginBtn').style.display = 'inline-block';\n";
        html += "            document.getElementById('registerBtn').style.display = 'inline-block';\n";
        html += "            document.getElementById('logoutBtn').style.display = 'none';\n";
        html += "            document.getElementById('sessionSelector').style.display = 'none';\n";
        html += "            document.getElementById('sessionInfo').textContent = '未登录';\n";
        html += "            document.getElementById('chatBox').innerHTML = '';\n";
        html += "            document.getElementById('status').textContent = '已退出登录';\n";
        html += "        }\n";
        html += "\n";
        html += "        // 创建新会话\n";
        html += "        function createNewSession() {\n";
        html += "            if (!currentUser.userId) {\n";
        html += "                alert('请先登录');\n";
        html += "                return;\n";
        html += "            }\n";
        html += "            \n";
        html += "            const sessionName = prompt('请输入会话名称:', '新会话');\n";
        html += "            if (!sessionName) return;\n";
        html += "            \n";
        html += "            document.getElementById('status').textContent = '创建会话中...';\n";
        html += "            \n";
        html += "            fetch('/api/chat', {\n";
        html += "                method: 'POST',\n";
        html += "                headers: { 'Content-Type': 'application/json' },\n";
        html += "                body: JSON.stringify({ \n";
        html += "                    message: '创建新会话', \n";
        html += "                    user_id: currentUser.userId,\n";
        html += "                    session_name: sessionName\n";
        html += "                })\n";
        html += "            })\n";
        html += "            .then(response => response.json())\n";
        html += "            .then(data => {\n";
        html += "                if (data.session_id) {\n";
        html += "                    // 更新当前会话\n";
        html += "                    currentUser.sessionId = data.session_id;\n";
        html += "                    localStorage.setItem('currentUser', JSON.stringify(currentUser));\n";
        html += "                    \n";
        html += "                    // 刷新会话列表\n";
        html += "                    loadUserSessions();\n";
        html += "                    document.getElementById('chatBox').innerHTML = '';\n";
        html += "                    document.getElementById('sessionInfo').textContent = `用户: ${currentUser.username} | 会话ID: ${currentUser.sessionId}`;\n";
        html += "                    document.getElementById('status').textContent = '已创建新会话';\n";
        html += "                } else {\n";
        html += "                    document.getElementById('status').textContent = '创建会话失败';\n";
        html += "                }\n";
        html += "            })\n";
        html += "            .catch(error => {\n";
        html += "                console.error('创建会话失败:', error);\n";
        html += "                document.getElementById('status').textContent = '创建会话失败';\n";
        html += "            });\n";
        html += "        }\n";
        html += "\n";
        html += "        // 切换会话\n";
        html += "        function changeSession(sessionId) {\n";
        html += "            if (!sessionId) return;\n";
        html += "            \n";
        html += "            currentUser.sessionId = sessionId;\n";
        html += "            localStorage.setItem('currentUser', JSON.stringify(currentUser));\n";
        html += "            \n";
        html += "            document.getElementById('sessionInfo').textContent = `用户: ${currentUser.username} | 会话ID: ${sessionId}`;\n";
        html += "            loadSessionHistory(sessionId);\n";
        html += "        }\n";
        html += "\n";
        html += "        // 添加消息到聊天框\n";
        html += "        function addMessage(content, isUser) {\n";
        html += "            const chatBox = document.getElementById('chatBox');\n";
        html += "            const messageDiv = document.createElement('div');\n";
        html += "            messageDiv.className = 'message ' + (isUser ? 'user-message' : 'bot-message');\n";
        html += "            \n";
        html += "            // 处理段落和换行\n";
        html += "            const paragraphs = content.split('\\n').filter(p => p.trim());\n";
        html += "            if (paragraphs.length > 1) {\n";
        html += "                paragraphs.forEach(p => {\n";
        html += "                    const para = document.createElement('p');\n";
        html += "                    para.textContent = p;\n";
        html += "                    para.style.margin = '5px 0';\n";
        html += "                    messageDiv.appendChild(para);\n";
        html += "                });\n";
        html += "            } else {\n";
        html += "                messageDiv.textContent = content;\n";
        html += "            }\n";
        html += "            \n";
        html += "            chatBox.appendChild(messageDiv);\n";
        html += "            chatBox.scrollTop = chatBox.scrollHeight;\n";
        html += "        }\n";
        html += "\n";
        html += "        // 发送消息\n";
        html += "        function sendMessage() {\n";
        html += "            const input = document.getElementById('messageInput');\n";
        html += "            const message = input.value.trim(); // 移除前后空白\n";
        html += "            \n";
        html += "            if (!message) {\n";
        html += "                alert('请输入消息内容');\n";
        html += "                return;\n";
        html += "            }\n";
        html += "            \n";
        html += "            addMessage(message, true);\n";
        html += "            input.value = '';\n";
        html += "            \n";
        html += "            const button = document.getElementById('sendButton');\n";
        html += "            button.disabled = true;\n";
        html += "            button.textContent = '发送中...';\n";
        html += "            \n";
        html += "            document.getElementById('status').textContent = '正在处理...';\n";
        html += "            \n";
        html += "            // 准备请求数据\n";
        html += "            const requestData = { message: message };\n";
        html += "            \n";
        html += "            // 如果已登录，添加用户ID和会话ID\n";
        html += "            if (currentUser.userId) {\n";
        html += "                requestData.user_id = currentUser.userId;\n";
        html += "            }\n";
        html += "            \n";
        html += "            if (currentUser.sessionId) {\n";
        html += "                requestData.session_id = currentUser.sessionId;\n";
        html += "            }\n";
        html += "            \n";
        html += "            fetch('/api/chat', {\n";
        html += "                method: 'POST',\n";
        html += "                headers: { 'Content-Type': 'application/json' },\n";
        html += "                body: JSON.stringify(requestData)\n";
        html += "            })\n";
        html += "            .then(response => response.json())\n";
        html += "            .then(data => {\n";
        html += "                if (data.response) {\n";
        html += "                    addMessage(data.response, false);\n";
        html += "                    \n";
        html += "                    // 如果收到新的会话ID，更新当前会话\n";
        html += "                    if (data.session_id && (!currentUser.sessionId || currentUser.sessionId !== data.session_id)) {\n";
        html += "                        currentUser.sessionId = data.session_id;\n";
        html += "                        localStorage.setItem('currentUser', JSON.stringify(currentUser));\n";
        html += "                        document.getElementById('sessionInfo').textContent = `用户: ${currentUser.username || '匿名'} | 会话ID: ${currentUser.sessionId}`;\n";
        html += "                    }\n";
        html += "                    \n";
        html += "                    if (data.cached) {\n";
        html += "                        document.getElementById('status').textContent = '已返回缓存结果';\n";
        html += "                    } else {\n";
        html += "                        document.getElementById('status').textContent = '响应完成';\n";
        html += "                    }\n";
        html += "                } else if (data.error) {\n";
        html += "                    addMessage(`错误: ${data.error}`, false);\n";
        html += "                    document.getElementById('status').textContent = '响应错误';\n";
        html += "                } else {\n";
        html += "                    addMessage('服务器返回了无效响应', false);\n";
        html += "                    document.getElementById('status').textContent = '响应错误';\n";
        html += "                }\n";
        html += "            })\n";
        html += "            .catch(error => {\n";
        html += "                console.error('Error:', error);\n";
        html += "                addMessage('请求失败: ' + error.message, false);\n";
        html += "                document.getElementById('status').textContent = '请求失败';\n";
        html += "            })\n";
        html += "            .finally(() => {\n";
        html += "                button.disabled = false;\n";
        html += "                button.textContent = '发送';\n";
        html += "            });\n";
        html += "        }\n";
        html += "\n";
        html += "        // 回车发送\n";
        html += "        document.getElementById('messageInput').addEventListener('keypress', function(e) {\n";
        html += "            if (e.key === 'Enter') {\n";
        html += "                sendMessage();\n";
        html += "            }\n";
        html += "        });\n";
        html += "\n";
        html += "        // 页面加载完成后初始化\n";
        html += "        document.addEventListener('DOMContentLoaded', function() {\n";
        html += "            initializeApp();\n";
        html += "            \n";
        html += "            // 加载状态\n";
        html += "            fetch('/api/status')\n";
        html += "                .then(response => response.json())\n";
        html += "                .then(data => {\n";
        html += "                    let statusText = `缓存: ${data.cache_size} | LLaMA: ${data.llama_available ? '可用' : '不可用'}`;\n";
        html += "                    if (data.database_available) {\n";
        html += "                        statusText += ` | DB: 可用`;\n";
        html += "                    } else {\n";
        html += "                        statusText += ` | DB: 不可用`;\n";
        html += "                    }\n";
        html += "                    document.getElementById('status').textContent = statusText;\n";
        html += "                })\n";
        html += "                .catch(() => document.getElementById('status').textContent = '状态获取失败');\n";
        html += "        });\n";
        html += "    </script>\n";
        html += "</body>\n";
        html += "</html>\n";
        
        return html;
    }

    HttpServer httpServer_;
    LlamaService llama_service_;
    KamaCache::KLfuCache<std::string, std::string> lfu_cache_;
};

// // 如果这个方法在HttpResponse类中不存在，需要添加
// void HttpResponse::setJsonResponse(const std::string& jsonStr) {
//     setStatusCode(HttpStatusCode::OK);
//     setStatusMessage("OK");
//     setHeader("Content-Type", "application/json; charset=utf-8");
//     setBody(jsonStr);
// }

// 生成简单的UUID作为会话标识符
std::string generateUUID() {
    // 使用当前时间和随机数生成唯一标识符
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 15);
    static std::uniform_int_distribution<> dis2(8, 11);

    std::stringstream ss;
    ss << std::hex;

    for (int i = 0; i < 8; i++) {
        ss << dis(gen);
    }
    ss << "-";

    for (int i = 0; i < 4; i++) {
        ss << dis(gen);
    }
    ss << "-4";  // 版本 4 UUID

    for (int i = 0; i < 3; i++) {
        ss << dis(gen);
    }
    ss << "-";
    
    ss << dis2(gen);
    
    for (int i = 0; i < 3; i++) {
        ss << dis(gen);
    }
    ss << "-";

    for (int i = 0; i < 12; i++) {
        ss << dis(gen);
    }

    return ss.str();
}

// 根据对话历史构建完整提示词
std::string buildPromptWithHistory(const std::vector<std::string>& history) {
    std::stringstream prompt;
    
    if (history.size() <= 1) {
        // 如果只有当前消息，直接返回
        return history.back();
    }
    
    // 构建对话历史格式
    // 注意：这里我们假设历史记录中的奇数条目是用户消息，偶数条目是AI回复
    // 实际应用中可能需要更复杂的管理方式
    
    size_t historyLimit = std::min(history.size(), size_t(10)); // 限制历史记录长度，防止提示词过长
    size_t startIdx = history.size() - historyLimit;
    
    for (size_t i = startIdx; i < history.size() - 1; i += 2) {
        prompt << "User: " << history[i] << "\n";
        if (i + 1 < history.size() - 1) {
            prompt << "AI: " << history[i + 1] << "\n\n";
        }
    }
    
    // 添加当前问题
    prompt << "User: " << history.back() << "\nAI:";
    
    return prompt.str();
}

// 添加清理非UTF-8字符的辅助函数
void cleanInvalidUtf8(std::string& str) {
    std::string result;
    result.reserve(str.size());
    
    for (size_t i = 0; i < str.size(); ) {
        unsigned char c = str[i];
        if (c < 0x80) {  // ASCII字符
            result.push_back(c);
            i++;
        } else if ((c & 0xE0) == 0xC0) {  // 2字节UTF-8
            if (i + 1 < str.size() && (str[i+1] & 0xC0) == 0x80) {
                result.push_back(c);
                result.push_back(str[i+1]);
                i += 2;
            } else {
                // 无效UTF-8
                result.push_back('?');
                i++;
            }
        } else if ((c & 0xF0) == 0xE0) {  // 3字节UTF-8
            if (i + 2 < str.size() && 
                (str[i+1] & 0xC0) == 0x80 && 
                (str[i+2] & 0xC0) == 0x80) {
                result.push_back(c);
                result.push_back(str[i+1]);
                result.push_back(str[i+2]);
                i += 3;
            } else {
                // 无效UTF-8
                result.push_back('?');
                i++;
            }
        } else if ((c & 0xF8) == 0xF0) {  // 4字节UTF-8
            if (i + 3 < str.size() && 
                (str[i+1] & 0xC0) == 0x80 && 
                (str[i+2] & 0xC0) == 0x80 && 
                (str[i+3] & 0xC0) == 0x80) {
                result.push_back(c);
                result.push_back(str[i+1]);
                result.push_back(str[i+2]);
                result.push_back(str[i+3]);
                i += 4;
            } else {
                // 无效UTF-8
                result.push_back('?');
                i++;
            }
        } else {
            // 无效UTF-8起始字节
            result.push_back('?');
            i++;
        }
    }
    
    str = result;
}

// 添加字符串替换辅助函数
void replaceAll(std::string& str, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = str.find(from, pos)) != std::string::npos) {
        str.replace(pos, from.length(), to);
        pos += to.length();
    }
}

// 验证和清理可能导致JSON解析错误的字符
std::string sanitizeJsonString(const std::string& input) {
    std::string result;
    result.reserve(input.size());
    
    for (char c : input) {
        // 控制字符可能导致JSON解析错误，替换为空格
        if ((unsigned char)c < 32) {
            if (c == '\n' || c == '\r' || c == '\t') {
                // 保留这些常见控制字符
                result.push_back(c);
            } else {
                // 其他控制字符替换为空格
                result.push_back(' ');
            }
        } else if (c == '"' || c == '\\') {
            // 在 JSON 字符串中转义引号和反斜杠
            result.push_back('\\');
            result.push_back(c);
        } else {
            result.push_back(c);
        }
    }
    
    return result;
}

int main(int argc, char* argv[]) {
    // 初始化日志系统
    const std::string LogDir = "logs";
    mkdir(LogDir.c_str(), 0755);
    std::ostringstream LogfilePath;
    LogfilePath << LogDir << "/" << ::basename(argv[0]);
    
    const int kRollSize = 1 * 1024 * 1024; // 1MB
    AsyncLogging log(LogfilePath.str(), kRollSize);
    Logger::setOutput([&log](const char* msg, int len) {
        log.append(msg, len);
    });
    log.start();
    
    // 初始化内存池
    memoryPool::HashBucket::initMemoryPool();
    
    // 初始化MySQL客户端库
    mysql_library_init(0, nullptr, nullptr);
    
    // 启动HTTP服务器
    EventLoop loop;
    InetAddress httpAddr(8081, "0.0.0.0");  // 修改为监听所有接口
    LlamaHttpService service(&loop, httpAddr);
    service.start();

    std::cout << "HTTP Server started on http://0.0.0.0:8081" << std::endl;
    loop.loop();
    
    // 清理MySQL库
    mysql_library_end();
    
    return 0;
}


