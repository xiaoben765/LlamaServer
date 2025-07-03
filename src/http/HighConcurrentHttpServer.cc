#include "http/HighConcurrentHttpServer.h"
#include "http/HttpContext.h"
#include "http/HttpRequest.h"
#include "http/HttpResponse.h"
#include "http/HttpParser.h"
#include "AsyncTaskQueue.h"
#include "Buffer.h"
#include <functional>
#include <iostream>
#include <string>

namespace kama {
namespace http {

HighConcurrentHttpServer::HighConcurrentHttpServer(
    EventLoop* loop,
    const InetAddress& addr,
    const std::string& name,
    int numThreads)
    : m_server(loop, addr, name) {
    
    // 设置线程数量
    if (numThreads > 0) {
        m_server.setThreadNum(numThreads);
    }
    
    // 设置HTTP回调
    m_server.setConnectionCallback(
        [this](const TcpConnectionPtr& conn) {
            // 连接建立或关闭时的回调
        }
    );
    
    m_server.setMessageCallback(
        [this](const TcpConnectionPtr& conn, Buffer* buf, Timestamp receiveTime) {
            HttpContext context;
            
            // 从Buffer中获取数据并解析请求
            std::string data(buf->peek(), buf->readableBytes());
            if (context.parseRequest(data.c_str(), data.size()) == HttpParseState::PARSE_ERROR) {
                // 解析失败，发送400错误
                conn->send("HTTP/1.1 400 Bad Request\r\n\r\n");
                conn->shutdown();
                return;
            }
            
            // 清除处理过的数据
            buf->retrieveAll();
            
            if (context.isComplete()) {
                onRequest(context.request(), conn);
                context.reset();
            }
        }
    );
}

void HighConcurrentHttpServer::start() {
    // 确保异步任务队列已初始化
    if (!AsyncTaskQueue::getInstance().init()) {
        std::cerr << "初始化异步任务队列失败" << std::endl;
    }
    
    m_server.start();
}

void HighConcurrentHttpServer::setThreadNum(int numThreads) {
    m_server.setThreadNum(numThreads);
}

void HighConcurrentHttpServer::registerHandler(const std::string& path, const HttpCallback& handler) {
    std::lock_guard<std::mutex> lock(m_handlerMutex);
    m_handlers[path] = handler;
}

void HighConcurrentHttpServer::registerAsyncHandler(const std::string& path, const AsyncHttpCallback& handler) {
    std::lock_guard<std::mutex> lock(m_handlerMutex);
    m_asyncHandlers[path] = handler;
}

bool HighConcurrentHttpServer::findHandler(const std::string& path, HttpCallback& handler, bool& isAsync, AsyncHttpCallback& asyncHandler) {
    std::lock_guard<std::mutex> lock(m_handlerMutex);
    
    // 首先查找精确匹配
    auto it = m_handlers.find(path);
    if (it != m_handlers.end()) {
        handler = it->second;
        isAsync = false;
        return true;
    }
    
    // 查找异步处理器
    auto asyncIt = m_asyncHandlers.find(path);
    if (asyncIt != m_asyncHandlers.end()) {
        asyncHandler = asyncIt->second;
        isAsync = true;
        return true;
    }
    
    // 查找前缀匹配
    for (const auto& pair : m_handlers) {
        if (path.find(pair.first) == 0) {
            handler = pair.second;
            isAsync = false;
            return true;
        }
    }
    
    for (const auto& pair : m_asyncHandlers) {
        if (path.find(pair.first) == 0) {
            asyncHandler = pair.second;
            isAsync = true;
            return true;
        }
    }
    
    return false;
}

void HighConcurrentHttpServer::onRequest(const HttpRequest& req, const TcpConnectionPtr& conn) {
    HttpResponse response;
    
    // 设置默认响应头
    response.setStatusCode(HttpStatusCode::OK);
    response.setStatusMessage("OK");
    response.addHeader("Server", "KamaWebServer");
    
    // 寻找处理器
    HttpCallback handler;
    AsyncHttpCallback asyncHandler;
    bool isAsync = false;
    
    bool found = findHandler(req.path(), handler, isAsync, asyncHandler);
    
    if (found) {
        if (isAsync) {
            // 异步处理
            asyncHandler(req, response, [conn, this](HttpResponse& resp) {
                handleAsyncResponse(conn, resp);
            });
        } else {
            // 同步处理
            handler(req, response);
            handleAsyncResponse(conn, response);
        }
    } else if (m_httpCallback) {
        // 使用默认处理器
        m_httpCallback(req, response);
        handleAsyncResponse(conn, response);
    } else {
        // 没有处理器，返回404
        response.setStatusCode(HttpStatusCode::NOT_FOUND);
        response.setStatusMessage("Not Found");
        response.addHeader("Connection", "close"); // 设置关闭连接
        handleAsyncResponse(conn, response);
    }
}

void HighConcurrentHttpServer::handleAsyncResponse(const TcpConnectionPtr& conn, HttpResponse& resp) {
    // 使用异步任务队列发送响应，避免阻塞IO线程
    AsyncTaskQueue::getInstance().submit([conn, resp]() {
        // 将响应转换为字符串
        std::string responseStr = resp.toString();
        conn->send(responseStr);
        
        // 检查是否需要关闭连接
        if (resp.getHeader("Connection") == "close") {
            conn->shutdown();
        }
    });
}

} // namespace http
} // namespace kama
