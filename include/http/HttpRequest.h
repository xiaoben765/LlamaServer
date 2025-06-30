#pragma once

#include <string>
#include <unordered_map>
#include <memory>

namespace kama {
namespace http {

enum class HttpMethod {
    UNKNOWN = 0,
    GET,
    POST,
    PUT,
    DELETE,
    OPTIONS,
    HEAD,
    PATCH
};

enum class HttpVersion {
    UNKNOWN = 0,
    HTTP10,
    HTTP11
};

class HttpRequest {
public:
    HttpRequest();
    ~HttpRequest() = default;

    // 基本属性获取
    HttpMethod method() const { return method_; }
    const std::string& path() const { return path_; }
    const std::string& query() const { return query_; }
    HttpVersion version() const { return version_; }
    const std::string& body() const { return body_; }

    // 头部信息操作
    void addHeader(const std::string& key, const std::string& value);
    std::string getHeader(const std::string& key) const;
    const std::unordered_map<std::string, std::string>& headers() const { return headers_; }

    // 设置方法
    void setMethod(HttpMethod method) { method_ = method; }
    void setPath(const std::string& path) { path_ = path; }
    void setQuery(const std::string& query) { query_ = query; }
    void setVersion(HttpVersion version) { version_ = version; }
    void setBody(const std::string& body) { body_ = body; }

    // 工具方法
    void reset();
    std::string methodString() const;
    std::string versionString() const;
    
    // 获取客户端地址（用于日志和速率限制）
    std::string clientAddr() const { return clientAddr_; }
    void setClientAddr(const std::string& addr) { clientAddr_ = addr; }

    // 静态工具方法
    static HttpMethod stringToMethod(const std::string& method);
    static HttpVersion stringToVersion(const std::string& version);

private:
    HttpMethod method_;
    std::string path_;
    std::string query_;
    HttpVersion version_;
    std::unordered_map<std::string, std::string> headers_;
    std::string body_;
    std::string clientAddr_; // 客户端地址
};

} // namespace http
} // namespace kama