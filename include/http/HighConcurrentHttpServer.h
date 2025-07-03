#pragma once

#include "http/HttpContext.h"
#include "http/HttpRequest.h"
#include "http/HttpResponse.h"
#include "TcpServer.h"
#include "AsyncTaskQueue.h"
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <mutex>

namespace kama {
namespace http {

class HttpConnection;

/**
 * @brief 高并发HTTP服务器
 * 
 * 提供异步处理HTTP请求的功能，支持高并发
 */
class HighConcurrentHttpServer {
public:
    using HttpCallback = std::function<void(const HttpRequest&, HttpResponse&)>;
    using AsyncHttpCallback = std::function<void(const HttpRequest&, HttpResponse&, std::function<void(HttpResponse&)>)>;
    
    /**
     * @brief 构造函数
     * 
     * @param loop 事件循环
     * @param addr 监听地址
     * @param name 服务器名称
     * @param numThreads 工作线程数
     */
    HighConcurrentHttpServer(EventLoop* loop,
                           const InetAddress& addr,
                           const std::string& name,
                           int numThreads = 0);
    
    /**
     * @brief 启动服务器
     */
    void start();
    
    /**
     * @brief 设置HTTP回调函数
     */
    void setHttpCallback(const HttpCallback& cb) {
        m_httpCallback = cb;
    }
    
    /**
     * @brief 注册同步HTTP处理器
     * 
     * @param path 路径
     * @param handler 处理函数
     */
    void registerHandler(const std::string& path, const HttpCallback& handler);
    
    /**
     * @brief 注册异步HTTP处理器
     * 
     * @param path 路径
     * @param handler 异步处理函数
     */
    void registerAsyncHandler(const std::string& path, const AsyncHttpCallback& handler);
    
    /**
     * @brief 设置线程数
     */
    void setThreadNum(int numThreads);

private:
    // 处理HTTP请求
    void onRequest(const HttpRequest& req, const TcpConnectionPtr& conn);
    
    // 找到合适的处理器
    bool findHandler(const std::string& path, HttpCallback& handler, bool& isAsync, AsyncHttpCallback& asyncHandler);
    
    // 处理异步响应
    void handleAsyncResponse(const TcpConnectionPtr& conn, HttpResponse& resp);
    
private:
    TcpServer m_server;
    HttpCallback m_httpCallback;
    
    // 处理器映射
    std::unordered_map<std::string, HttpCallback> m_handlers;
    std::unordered_map<std::string, AsyncHttpCallback> m_asyncHandlers;
    std::mutex m_handlerMutex;
};

} // namespace http
} // namespace kama
