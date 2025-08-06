#pragma once

#include "http/HttpServer.h"
#include "services/IDatabaseService.h"
#include "services/ILlamaService.h"
#include <memory>
#include <string>
#include <ctime>

namespace llama {
namespace http {

class LlamaHttpApplication {
public:
    LlamaHttpApplication(
        EventLoop* loop, 
        const InetAddress& addr,
        const std::string& staticFilesRoot
    );
    
    void start();
    
private:
    // 配置加载
    void loadConfig();
    
    // 中间件设置
    void setupMiddleware();
    
    // 服务初始化
    void initServices();
    
    // 路由设置
    void setupRoutes();
    
    // API处理器
    void handleModelsRequest(const HttpRequest& req, HttpResponse& resp);
    void handleLlamaQuery(const HttpRequest& req, HttpResponse& resp);
    void handleStatusRequest(const HttpRequest& req, HttpResponse& resp);
    
    // 管理员API处理器
    void handleClearCacheRequest(const HttpRequest& req, HttpResponse& resp);
    void handleCacheContentRequest(const HttpRequest& req, HttpResponse& resp);
    void handleClearDatabaseRequest(const HttpRequest& req, HttpResponse& resp);
    void handleDatabaseStatsRequest(const HttpRequest& req, HttpResponse& resp);
    
    // 用户管理API处理器
    void handleUsersRequest(const HttpRequest& req, HttpResponse& resp);
    void handleClearUsersRequest(const HttpRequest& req, HttpResponse& resp);
    void handleDeleteUserRequest(const HttpRequest& req, HttpResponse& resp);
    
    // 认证API处理器
    void handleUserRegisterRequest(const HttpRequest& req, HttpResponse& resp);
    void handleUserLoginRequest(const HttpRequest& req, HttpResponse& resp);
    void handleUserLogoutRequest(const HttpRequest& req, HttpResponse& resp);
    
    // 对话API处理器
    void handleConversationsRequest(const HttpRequest& req, HttpResponse& resp);
    
    // 工具函数
    std::string formatTimestamp(time_t timestamp);

private:
    HttpServer server_;
    
    // 配置项
    int port_;
    int threadNum_;
    std::string llamaServiceType_;
    std::string llamaModelPath_;
    std::string llamaServerIp_;
    int llamaServerPort_;
    std::string dbType_;
    bool developmentMode_;
    std::time_t startTime_;
    
    // 服务组件
    services::IDatabaseService* dbService_;
    std::unique_ptr<services::ILlamaService> llamaService_;
};

} // namespace http
} // namespace llama
