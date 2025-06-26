#pragma once

#include "TcpServer.h"
#include "HttpRequest.h"
#include "HttpResponse.h"
#include "HttpContext.h"
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

private:
    void onConnection(const TcpConnectionPtr& conn);
    void onMessage(const TcpConnectionPtr& conn, Buffer* buf, Timestamp time);
    
    void handleHttpRequest(const TcpConnectionPtr& conn, const HttpRequest& req);
    void sendHttpResponse(const TcpConnectionPtr& conn, const HttpResponse& resp);
    
    RouteHandler findRoute(const std::string& method, const std::string& path);
    void handleStaticFile(const HttpRequest& req, HttpResponse& resp);
    void handleNotFound(const HttpRequest& req, HttpResponse& resp);
    void handleError(const HttpRequest& req, HttpResponse& resp, const std::string& error);
    
    std::string makeRouteKey(const std::string& method, const std::string& path);
    
    TcpServer server_;
    std::string staticFileRoot_;
    bool enableStaticFiles_;
    std::string serverName_; // 存储服务器名称
    std::string ipPortStr_; // 保存IP和端口信息
    
    // 路由表
    std::unordered_map<std::string, RouteHandler> routes_;
};

} // namespace http
} // namespace kama