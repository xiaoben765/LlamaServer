#pragma once

#include "TcpServer.h"
#include "http/HttpRequest.h"
#include "http/HttpResponse.h"
#include "http/HttpContext.h"
#include "http/Middleware.h"  // 添加中间件支持
#include <functional>
#include <memory>
#include <unordered_map>

namespace kama {
namespace http {

class HttpServer {
public:
    // 定义路由处理器类型
    using RouteHandler = std::function<void(const HttpRequest&, HttpResponse&)>;

    HttpServer(EventLoop* loop, const InetAddress& addr, const std::string& name);
    ~HttpServer() = default;

    // 服务器控制
    void start();
    void setThreadNum(int numThreads);
    
    // 路由注册
    void addRoute(const std::string& method, const std::string& path, RouteHandler handler);
    void get(const std::string& path, RouteHandler handler);
    void post(const std::string& path, RouteHandler handler);
    
    // 静态文件服务
    void setStaticFileRoot(const std::string& root) { staticFileRoot_ = root; }
    void enableStaticFiles(bool enable = true) { enableStaticFiles_ = enable; }
    void setDevelopmentMode(bool devMode = true) { developmentMode_ = devMode; }

    // 中间件支持
    void use(std::shared_ptr<IMiddleware> middleware) {
        middlewareChain_.use(std::move(middleware));
    }
    
    // 方便中间件添加的辅助函数
    void enableLogging() {
        use(std::make_shared<LoggingMiddleware>());
    }
    
    void enableCors(const std::string& allowOrigin = "*") {
        use(std::make_shared<CorsMiddleware>(allowOrigin));
    }
    
    void enableRateLimit(int maxRequests, int perSeconds) {
        use(std::make_shared<RateLimitMiddleware>(maxRequests, perSeconds));
    }
    
    void enableCompression() {
        use(std::make_shared<CompressionMiddleware>());
    }
    
    std::shared_ptr<AuthMiddleware> enableAuth() {
        auto authMiddleware = std::make_shared<AuthMiddleware>();
        use(authMiddleware);
        return authMiddleware;
    }

private:
    void onConnection(const ::TcpConnectionPtr& conn);
    void onMessage(const ::TcpConnectionPtr& conn, Buffer* buf, Timestamp time);
    
    void handleHttpRequest(const ::TcpConnectionPtr& conn, const HttpRequest& req);
    void sendHttpResponse(const ::TcpConnectionPtr& conn, const HttpResponse& resp);
    
    RouteHandler findRoute(const std::string& method, const std::string& path);
    void handleStaticFile(const HttpRequest& req, HttpResponse& resp);
    void handleNotFound(const HttpRequest& req, HttpResponse& resp);
    void handleError(const HttpRequest& req, HttpResponse& resp, const std::string& error);
    
    std::string makeRouteKey(const std::string& method, const std::string& path);
    
    TcpServer server_;
    std::string staticFileRoot_;
    bool enableStaticFiles_;
    bool developmentMode_;  // 开发模式标识
    std::string serverName_; // 存储服务器名称
    std::string ipPortStr_; // 保存IP和端口信息
    
    // 路由表
    std::unordered_map<std::string, RouteHandler> routes_;
    
    // 中间件链
    MiddlewareChain middlewareChain_;
};

} // namespace http
} // namespace kama