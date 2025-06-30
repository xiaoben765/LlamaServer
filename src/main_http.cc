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
        // 移除根路径的专门处理 - 让静态文件处理器处理它
        // 静态文件处理器会自动将 "/" 映射到 "/index.html"
        
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
        // 使用绝对路径设置静态文件根目录
        std::string projectRoot = "/home/shl203/kama-webserver";
        std::string staticRoot = projectRoot + "/static";
        httpServer_.setStaticFileRoot(staticRoot);
        httpServer_.enableStaticFiles(true);
        LOG_INFO << "静态文件服务已启用，根目录: " << staticRoot;
        
        // 注意: HttpServer类目前没有setDefaultDocuments方法
        // 默认情况下，大多数Web服务器会自动查找index.html作为默认文档
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
                // 检查会话ID是否为特殊值，需要创建新会话
                if (sessionId == "default" || sessionId.empty()) {
                    sessionId = kama::db::DatabaseManager::instance().createSession(userId, "默认会话");
                    if (!sessionId.empty()) {
                        LOG_INFO << "创建新会话: " << sessionId << " 用户: " << userId;
                    } else {
                        sessionId = "session_" + generateUUID(); // 使用临时会话ID
                        LOG_WARN << "数据库创建会话失败，使用临时会话ID: " << sessionId;
                    }
                } else {
                    // 检查会话是否真的存在
                    bool sessionExists = kama::db::DatabaseManager::instance().sessionExists(sessionId);
                    
                    // 如果会话不存在，创建新会话
                    if (!sessionExists) {
                        LOG_WARN << "会话ID '" << sessionId << "' 不存在，创建新会话";
                        std::string newSessionId = kama::db::DatabaseManager::instance().createSession(userId, "自动创建会话");
                        if (!newSessionId.empty()) {
                            sessionId = newSessionId;
                            LOG_INFO << "已自动创建新会话: " << sessionId << " 用户: " << userId;
                        }
                    } else {
                        // 更新会话活动时间
                        kama::db::DatabaseManager::instance().updateSessionActivity(sessionId);
                    }
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
        // 添加CORS头，允许跨域访问
        resp.enableCORS();
        
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
    
    // 类的成员变量
    HttpServer httpServer_;
    LlamaService llama_service_;
    KamaCache::KLfuCache<std::string, std::string> lfu_cache_;
};

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


