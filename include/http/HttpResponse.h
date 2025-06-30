#pragma once

#include <string>
#include <unordered_map>
#include "HttpRequest.h"

namespace kama {
namespace http {

enum class HttpStatusCode {
    OK = 200,
    CREATED = 201,
    NO_CONTENT = 204,
    MOVED_PERMANENTLY = 301,  // 添加301重定向状态码
    BAD_REQUEST = 400,
    UNAUTHORIZED = 401,
    FORBIDDEN = 403,
    NOT_FOUND = 404,
    METHOD_NOT_ALLOWED = 405,
    CONFLICT = 409,
    TOO_MANY_REQUESTS = 429,  // 添加速率限制状态码
    INTERNAL_SERVER_ERROR = 500,
    NOT_IMPLEMENTED = 501,
    BAD_GATEWAY = 502,
    SERVICE_UNAVAILABLE = 503
};

class HttpResponse {
public:
    HttpResponse(HttpVersion version = HttpVersion::HTTP11);
    ~HttpResponse() = default;

    // 状态设置
    void setStatusCode(HttpStatusCode code);
    void setStatusMessage(const std::string& message);

    // 头部设置
    void setHeader(const std::string& key, const std::string& value);
    void setContentType(const std::string& type);
    void setContentLength(size_t length);

    // 设置CORS头，允许跨域请求
    void enableCORS();

    // 内容设置
    void setBody(const std::string& body);
    void appendBody(const std::string& content);

    // 获取完整响应
    std::string toString() const;

    // 快捷方法
    void setJsonResponse(const std::string& json);
    void setHtmlResponse(const std::string& html);
    void setTextResponse(const std::string& text);
    void setErrorResponse(HttpStatusCode code, const std::string& message);

    // 获取属性
    HttpStatusCode statusCode() const { return statusCode_; }
    const std::string& statusMessage() const { return statusMessage_; }
    const std::string& body() const { return body_; }
    
    // 获取/添加头部信息
    std::string getHeader(const std::string& key) const;
    void addHeader(const std::string& key, const std::string& value) { setHeader(key, value); }
    const std::unordered_map<std::string, std::string>& headers() const { return headers_; }

private:
    HttpVersion version_;
    HttpStatusCode statusCode_;
    std::string statusMessage_;
    std::unordered_map<std::string, std::string> headers_;
    std::string body_;

    std::string getStatusString(HttpStatusCode code) const;
};

} // namespace http
} // namespace kama