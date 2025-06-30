#include "http/Middleware.h"
#include "Logger.h"
#include <chrono>
#include <zlib.h>
#include <sstream>
#include <iomanip>
#include <mutex>
#include <algorithm>

namespace kama {
namespace http {

// MiddlewareChain实现

void MiddlewareChain::use(std::shared_ptr<IMiddleware> middleware) {
    middlewares_.push_back(std::move(middleware));
    LOG_INFO << "添加中间件: " << middlewares_.back()->getName();
}

void MiddlewareChain::execute(const HttpRequest& request, HttpResponse& response, 
                              MiddlewareCallback callback) {
    if (middlewares_.empty()) {
        // 如果没有中间件，直接执行回调
        callback(request, response);
        return;
    }
    
    // 从第一个中间件开始执行
    executeNext(0, request, response, std::move(callback));
}

void MiddlewareChain::executeNext(size_t index, const HttpRequest& request, HttpResponse& response, 
                                 MiddlewareCallback callback) {
    if (index >= middlewares_.size()) {
        // 所有中间件执行完毕，执行最终回调
        callback(request, response);
        return;
    }
    
    auto& middleware = middlewares_[index];
    
    // 创建下一个处理器的函数
    auto next = [this, index, &request, &callback](const HttpRequest& req, HttpResponse& resp) {
        executeNext(index + 1, req, resp, callback);
    };
    
    // 执行当前中间件
    middleware->process(request, response, next);
}

// LoggingMiddleware实现

void LoggingMiddleware::process(const HttpRequest& request, HttpResponse& response, NextHandler next) {
    auto start = std::chrono::high_resolution_clock::now();
    
    // 记录请求信息
    LOG_INFO << "HTTP " << request.methodString() << " " << request.path() 
             << " 从 " << request.clientAddr();
    
    // 继续处理链
    next(request, response);
    
    // 计算处理时间
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    
    // 记录响应信息
    LOG_INFO << "HTTP " << request.methodString() << " " << request.path() 
             << " 响应 " << static_cast<int>(response.statusCode()) 
             << " (" << duration << "ms)";
}

// AuthMiddleware实现

void AuthMiddleware::addPath(const std::string& path, bool requireAuth) {
    authPaths_[path] = requireAuth;
}

void AuthMiddleware::process(const HttpRequest& request, HttpResponse& response, NextHandler next) {
    // 检查路径是否需要认证
    auto it = authPaths_.find(request.path());
    bool requireAuth = (it != authPaths_.end() && it->second);
    
    if (requireAuth) {
        // 获取认证头
        auto authHeader = request.getHeader("Authorization");
        
        if (authHeader.empty()) {
            // 没有认证头，返回401
            LOG_WARN << "访问需要认证的路径但未提供凭据: " << request.path();
            response.setStatusCode(HttpStatusCode::UNAUTHORIZED);
            response.setStatusMessage("Unauthorized");
            response.addHeader("WWW-Authenticate", "Basic realm=\"Kama WebServer\"");
            response.setBody("认证失败：需要提供有效的认证信息");
            return; // 不继续执行中间件链
        }
        
        // 这里可以添加更复杂的认证逻辑，如检查JWT令牌、会话等
        // 简单起见，这里只检查是否有Authorization头
        
        LOG_INFO << "已认证用户访问: " << request.path();
    }
    
    // 继续处理链
    next(request, response);
}

// CorsMiddleware实现

CorsMiddleware::CorsMiddleware(const std::string& allowOrigin)
    : allowOrigin_(allowOrigin) {
}

void CorsMiddleware::process(const HttpRequest& request, HttpResponse& response, NextHandler next) {
    // 添加CORS头
    response.addHeader("Access-Control-Allow-Origin", allowOrigin_);
    response.addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    response.addHeader("Access-Control-Allow-Headers", "Content-Type, Authorization");
    
    // 处理预检请求
    if (request.methodString() == "OPTIONS") {
        response.setStatusCode(HttpStatusCode::OK);
        response.setStatusMessage("OK");
        // 预检请求不继续执行
        return;
    }
    
    // 继续处理链
    next(request, response);
}

// RateLimitMiddleware实现

RateLimitMiddleware::RateLimitMiddleware(int maxRequests, int perSeconds)
    : maxRequests_(maxRequests), perSeconds_(perSeconds) {
}

void RateLimitMiddleware::process(const HttpRequest& request, HttpResponse& response, NextHandler next) {
    std::string clientIp = request.clientAddr();
    
    // 检查是否超过速率限制
    if (isRateLimited(clientIp)) {
        LOG_WARN << "客户端 " << clientIp << " 请求频率过高，已限制";
        
        // 返回429 Too Many Requests
        response.setStatusCode(HttpStatusCode::TOO_MANY_REQUESTS);
        response.setStatusMessage("Too Many Requests");
        response.addHeader("Retry-After", "5"); // 建议5秒后重试
        response.setBody("请求频率过高，请稍后重试");
        return; // 不继续执行中间件链
    }
    
    // 记录请求
    recordRequest(clientIp);
    
    // 继续处理链
    next(request, response);
}

bool RateLimitMiddleware::isRateLimited(const std::string& clientIp) {
    std::lock_guard<std::mutex> guard(mutex_);
    
    // 获取当前时间
    long now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    // 获取客户端的请求历史
    auto& times = requestTimes_[clientIp];
    
    // 清理过期的请求记录（早于perSeconds_秒前的记录）
    times.erase(
        std::remove_if(times.begin(), times.end(),
                     [now, this](long time) { return now - time > perSeconds_; }),
        times.end()
    );
    
    // 检查是否超过限制
    return times.size() >= maxRequests_;
}

void RateLimitMiddleware::recordRequest(const std::string& clientIp) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // 记录请求时间
    long now = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    
    requestTimes_[clientIp].push_back(now);
}

// CompressionMiddleware实现

void CompressionMiddleware::process(const HttpRequest& request, HttpResponse& response, NextHandler next) {
    // 先执行下一个中间件，以便获取完整的响应
    next(request, response);
    
    // 检查是否应该压缩响应
    if (shouldCompress(request, response)) {
        // 获取原始响应内容
        std::string originalBody = response.body();
        
        // 压缩内容
        std::string compressedBody = compress(originalBody);
        
        // 如果压缩有效（压缩后尺寸更小）
        if (compressedBody.size() < originalBody.size()) {
            LOG_INFO << "压缩响应从 " << originalBody.size() << " 字节到 " 
                    << compressedBody.size() << " 字节";
            
            response.setBody(compressedBody);
            response.addHeader("Content-Encoding", "gzip");
        }
    }
}

bool CompressionMiddleware::shouldCompress(const HttpRequest& request, const HttpResponse& response) {
    // 检查Accept-Encoding头
    std::string acceptEncoding = request.getHeader("Accept-Encoding");
    if (acceptEncoding.find("gzip") == std::string::npos) {
        return false; // 客户端不支持gzip
    }
    
    // 检查内容类型
    std::string contentType = response.getHeader("Content-Type");
    bool isCompressible = 
        contentType.find("text/") != std::string::npos ||
        contentType.find("application/json") != std::string::npos ||
        contentType.find("application/javascript") != std::string::npos ||
        contentType.find("application/xml") != std::string::npos;
    
    // 检查响应大小
    bool isSizeWorthy = response.body().size() > 1024; // 至少1KB才压缩
    
    return isCompressible && isSizeWorthy;
}

std::string CompressionMiddleware::compress(const std::string& data) {
    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    
    if (deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 
                    31, // 15 + 16 for gzip
                    8, Z_DEFAULT_STRATEGY) != Z_OK) {
        LOG_ERROR << "初始化zlib失败";
        return data; // 返回未压缩的数据
    }
    
    zs.next_in = (Bytef*)data.data();
    zs.avail_in = data.size();
    
    // 预估压缩后的大小（一般不会超过原始大小）
    size_t outSize = data.size();
    std::string outData(outSize, 0);
    
    zs.next_out = (Bytef*)outData.data();
    zs.avail_out = outSize;
    
    // 压缩
    int ret = deflate(&zs, Z_FINISH);
    
    // 如果输出缓冲区不够大
    if (ret == Z_BUF_ERROR || zs.avail_out == 0) {
        LOG_ERROR << "压缩缓冲区不足";
        deflateEnd(&zs);
        return data; // 返回未压缩的数据
    }
    
    // 清理
    deflateEnd(&zs);
    
    // 调整大小以匹配实际压缩数据
    size_t compressedSize = outSize - zs.avail_out;
    outData.resize(compressedSize);
    
    return outData;
}

} // namespace http
} // namespace kama
