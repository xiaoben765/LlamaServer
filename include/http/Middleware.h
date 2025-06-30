#pragma once

#include "TcpConnection.h"
#include "http/HttpRequest.h"
#include "http/HttpResponse.h"
#include "TcpConnection.h"
#include <functional>
#include <memory>
#include <vector>
#include <string>
#include <mutex>

namespace kama {
namespace http {

// 前向声明
class HttpContext;

using TcpConnectionPtr = std::shared_ptr<TcpConnection>;

/**
 * @brief HTTP中间件接口
 * 
 * 定义了HTTP请求处理管道中的中间件组件接口
 */
class IMiddleware {
public:
    using NextHandler = std::function<void(const HttpRequest&, HttpResponse&)>;
    
    virtual ~IMiddleware() = default;
    
    // 处理请求的主方法
    virtual void process(const HttpRequest& request, HttpResponse& response, NextHandler next) = 0;
    
    // 获取中间件名称（用于日志记录）
    virtual std::string getName() const = 0;
};

// 中间件处理完成后的回调函数
using MiddlewareCallback = std::function<void(const HttpRequest&, HttpResponse&)>;

/**
 * @brief HTTP中间件链管理器
 * 
 * 管理HTTP请求处理中间件链，按照注册顺序依次执行中间件
 */
class MiddlewareChain {
public:
    MiddlewareChain() = default;
    
    // 添加中间件
    void use(std::shared_ptr<IMiddleware> middleware);
    
    // 执行中间件链
    void execute(const HttpRequest& request, HttpResponse& response, MiddlewareCallback callback);
    
private:
    std::vector<std::shared_ptr<IMiddleware>> middlewares_;
    
    // 递归执行中间件链的辅助函数
    void executeNext(size_t index, const HttpRequest& request, HttpResponse& response, 
                     MiddlewareCallback callback);
};

/**
 * @brief 日志中间件
 * 
 * 记录HTTP请求和响应的日志
 */
class LoggingMiddleware : public IMiddleware {
public:
    LoggingMiddleware() = default;
    
    void process(const HttpRequest& request, HttpResponse& response, NextHandler next) override;
    std::string getName() const override { return "LoggingMiddleware"; }
};

/**
 * @brief 身份验证中间件
 * 
 * 验证用户身份，如果需要认证但未提供有效凭据，则返回401错误
 */
class AuthMiddleware : public IMiddleware {
public:
    AuthMiddleware() = default;
    
    // 添加一条路径，指定是否需要身份验证
    void addPath(const std::string& path, bool requireAuth);
    
    void process(const HttpRequest& request, HttpResponse& response, NextHandler next) override;
    std::string getName() const override { return "AuthMiddleware"; }
    
private:
    std::unordered_map<std::string, bool> authPaths_; // 路径->是否需要认证
};

/**
 * @brief CORS中间件
 * 
 * 处理跨域资源共享请求
 */
class CorsMiddleware : public IMiddleware {
public:
    CorsMiddleware(const std::string& allowOrigin = "*");
    
    void process(const HttpRequest& request, HttpResponse& response, NextHandler next) override;
    std::string getName() const override { return "CorsMiddleware"; }
    
private:
    std::string allowOrigin_;
};

/**
 * @brief 速率限制中间件
 * 
 * 限制请求频率，防止过度使用API
 */
class RateLimitMiddleware : public IMiddleware {
public:
    RateLimitMiddleware(int maxRequests, int perSeconds);
    
    void process(const HttpRequest& request, HttpResponse& response, NextHandler next) override;
    std::string getName() const override { return "RateLimitMiddleware"; }
    
private:
    int maxRequests_;
    int perSeconds_;
    std::unordered_map<std::string, std::vector<long>> requestTimes_; // IP -> 请求时间列表
    std::mutex mutex_;
    
    bool isRateLimited(const std::string& clientIp);
    void recordRequest(const std::string& clientIp);
};

/**
 * @brief 压缩中间件
 * 
 * 对响应内容进行压缩，减少传输数据量
 */
class CompressionMiddleware : public IMiddleware {
public:
    CompressionMiddleware() = default;
    
    void process(const HttpRequest& request, HttpResponse& response, NextHandler next) override;
    std::string getName() const override { return "CompressionMiddleware"; }
    
private:
    bool shouldCompress(const HttpRequest& request, const HttpResponse& response);
    std::string compress(const std::string& data);
};

} // namespace http
} // namespace kama
