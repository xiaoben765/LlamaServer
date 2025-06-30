#include "http/HttpServer.h"
#include "http/HttpContext.h"
#include "Buffer.h"
#include "Logger.h"
#include <fstream>
#include <unordered_map>
#include <memory>

namespace kama {
namespace http {

// TcpConnection 上下文类型，用于关联 HttpContext 到 TcpConnection
struct HttpConnectionContext {
    HttpContext context;
    // 可以添加其他与连接相关的数据
};

// 全局连接上下文映射表 - 用于替代TcpConnection中不存在的context机制
static std::unordered_map<std::string, std::shared_ptr<HttpConnectionContext>> g_connectionContexts;
static std::mutex g_connectionMutex;

HttpServer::HttpServer(EventLoop* loop, const InetAddress& addr, const std::string& name)
    : server_(loop, addr, name)
    , enableStaticFiles_(false)
    , serverName_(name)
    , ipPortStr_(addr.toIpPort()) // 在构造时保存IP:端口信息
{
    server_.setConnectionCallback(
        std::bind(&HttpServer::onConnection, this, std::placeholders::_1));
    server_.setMessageCallback(
        std::bind(&HttpServer::onMessage, this, std::placeholders::_1, 
                 std::placeholders::_2, std::placeholders::_3));
}

void HttpServer::start() {
    LOG_INFO << "HTTP Server [" << serverName_ << "] started on " << ipPortStr_;
    server_.start();
}

void HttpServer::setThreadNum(int numThreads) {
    server_.setThreadNum(numThreads);
}

void HttpServer::get(const std::string& path, RouteHandler handler) {
    addRoute("GET", path, handler);
}

void HttpServer::post(const std::string& path, RouteHandler handler) {
    addRoute("POST", path, handler);
}

void HttpServer::addRoute(const std::string& method, const std::string& path, RouteHandler handler) {
    std::string key = makeRouteKey(method, path);
    routes_[key] = handler;
    LOG_INFO << "Added route: " << method << " " << path;
}

void HttpServer::onConnection(const TcpConnectionPtr& conn) {
    std::string connName = conn->name();
    
    if (conn->connected()) {
        // 创建新的HTTP上下文并保存到全局映射表
        std::lock_guard<std::mutex> lock(g_connectionMutex);
        g_connectionContexts[connName] = std::make_shared<HttpConnectionContext>();
        LOG_INFO << "New HTTP connection from: " << conn->peerAddress().toIpPort();
    } else {
        // 连接关闭时删除上下文
        std::lock_guard<std::mutex> lock(g_connectionMutex);
        g_connectionContexts.erase(connName);
        LOG_INFO << "HTTP connection closed: " << conn->peerAddress().toIpPort();
    }
}

void HttpServer::onMessage(const TcpConnectionPtr& conn, Buffer* buf, Timestamp time) {
    std::string connName = conn->name();
    
    // 获取连接的HTTP上下文
    std::shared_ptr<HttpConnectionContext> contextPtr;
    {
        std::lock_guard<std::mutex> lock(g_connectionMutex);
        auto it = g_connectionContexts.find(connName);
        if (it != g_connectionContexts.end()) {
            contextPtr = it->second;
        }
    }
    
    if (!contextPtr) {
        LOG_ERROR << "No HTTP context found for connection";
        conn->shutdown();
        return;
    }
    
    HttpContext& context = contextPtr->context;
    HttpParseState state = context.parseRequest(buf->peek(), buf->readableBytes());
    
    if (state == HttpParseState::PARSE_ERROR) {
        HttpResponse response;
        response.setErrorResponse(HttpStatusCode::BAD_REQUEST, "Bad Request");
        sendHttpResponse(conn, response);
        conn->shutdown();
        return;
    }
    
    buf->retrieveAll();
    
    if (state == HttpParseState::GOT_ALL) {
        handleHttpRequest(conn, context.request());
        context.reset();
    }
}

void HttpServer::handleHttpRequest(const TcpConnectionPtr& conn, const HttpRequest& req) {
    HttpResponse response;
    
    try {
        // 查找路由
        RouteHandler handler = findRoute(req.methodString(), req.path());
        
        if (handler) {
            handler(req, response);
        } else if (enableStaticFiles_) {
            handleStaticFile(req, response);
        } else {
            handleNotFound(req, response);
        }
    } catch (const std::exception& e) {
        handleError(req, response, e.what());
    }
    
    sendHttpResponse(conn, response);
    
    // HTTP/1.0 或者没有 Keep-Alive 就关闭连接
    if (req.version() == HttpVersion::HTTP10 || 
        req.getHeader("Connection") == "close") {
        conn->shutdown();
    }
}

void HttpServer::sendHttpResponse(const TcpConnectionPtr& conn, const HttpResponse& resp) {
    std::string responseStr = resp.toString();
    conn->send(responseStr);
}

std::string HttpServer::makeRouteKey(const std::string& method, const std::string& path) {
    return method + ":" + path;
}

HttpServer::RouteHandler HttpServer::findRoute(const std::string& method, const std::string& path) {
    std::string key = makeRouteKey(method, path);
    auto it = routes_.find(key);
    return it != routes_.end() ? it->second : RouteHandler();
}

void HttpServer::handleNotFound(const HttpRequest& req, HttpResponse& resp) {
    resp.setErrorResponse(HttpStatusCode::NOT_FOUND, "Not Found");
}

void HttpServer::handleError(const HttpRequest& req, HttpResponse& resp, const std::string& error) {
    LOG_ERROR << "HTTP request error: " << error;
    resp.setErrorResponse(HttpStatusCode::INTERNAL_SERVER_ERROR, "Internal Server Error");
}

void HttpServer::handleStaticFile(const HttpRequest& req, HttpResponse& resp) {
    // 实现静态文件处理逻辑
    std::string path = req.path();
    
    // 处理根路径，加载index.html
    if (path == "/" || path.empty()) {
        path = "/index.html";
    }
    
    std::string filePath = staticFileRoot_ + path;
    
    // 安全检查: 防止目录遍历攻击
    if (path.find("..") != std::string::npos) {
        resp.setErrorResponse(HttpStatusCode::FORBIDDEN, "Access denied");
        return;
    }
    
    // 检查文件是否存在
    std::ifstream file(filePath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        handleNotFound(req, resp);
        return;
    }
    
    // 获取文件大小
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    // 读取文件内容
    std::string content(size, '\0');
    if (!file.read(&content[0], size)) {
        resp.setErrorResponse(HttpStatusCode::INTERNAL_SERVER_ERROR, "Failed to read file");
        return;
    }
    
    // 设置Content-Type
    std::string extension = filePath.substr(filePath.find_last_of('.') + 1);
    std::string contentType = "application/octet-stream"; // 默认
    
    if (extension == "html" || extension == "htm") {
        contentType = "text/html";
    } else if (extension == "css") {
        contentType = "text/css";
    } else if (extension == "js") {
        contentType = "application/javascript";
    } else if (extension == "png") {
        contentType = "image/png";
    } else if (extension == "jpg" || extension == "jpeg") {
        contentType = "image/jpeg";
    } else if (extension == "gif") {
        contentType = "image/gif";
    } else if (extension == "svg") {
        contentType = "image/svg+xml";
    } else if (extension == "json") {
        contentType = "application/json";
    } else if (extension == "txt") {
        contentType = "text/plain";
    }
    
    resp.setContentType(contentType);
    resp.enableCORS(); // 添加CORS头，允许跨域访问
    resp.setBody(content);
}

} // namespace http
} // namespace kama