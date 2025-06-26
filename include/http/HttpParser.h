#pragma once

#include "HttpRequest.h"
#include <memory>

namespace kama {
namespace http {

enum class HttpParseState {
    EXPECT_REQUEST_LINE,
    EXPECT_HEADERS,
    EXPECT_BODY,
    GOT_ALL,
    PARSE_ERROR
};

class HttpParser {
public:
    HttpParser();
    ~HttpParser() = default;

    // 解析接口
    HttpParseState parseRequest(const char* data, size_t len);
    
    // 获取解析结果
    const HttpRequest& request() const { return request_; }
    HttpRequest& request() { return request_; }
    
    // 状态查询
    bool hasError() const { return state_ == HttpParseState::PARSE_ERROR; }
    bool isComplete() const { return state_ == HttpParseState::GOT_ALL; }
    
    // 重置解析器
    void reset();

private:
    HttpParseState state_;
    HttpRequest request_;
    std::string buffer_;
    size_t contentLength_;
    
    bool parseRequestLine(const std::string& line);
    bool parseHeader(const std::string& line);
    void parseBody(const char* data, size_t len);
    
    std::string trim(const std::string& str);
    std::pair<std::string, std::string> splitHeader(const std::string& header);
};

} // namespace http
} // namespace kama