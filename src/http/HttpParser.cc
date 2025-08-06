#include "http/HttpParser.h"
#include <sstream>
#include <algorithm>

namespace llama {
namespace http {

HttpParser::HttpParser()
    : state_(HttpParseState::EXPECT_REQUEST_LINE)
    , contentLength_(0)
{
}

HttpParseState HttpParser::parseRequest(const char* data, size_t len) {
    buffer_.append(data, len);
    
    while (state_ != HttpParseState::GOT_ALL && state_ != HttpParseState::PARSE_ERROR) {
        if (state_ == HttpParseState::EXPECT_REQUEST_LINE) {
            size_t crlf = buffer_.find("\r\n");
            if (crlf != std::string::npos) {
                std::string requestLine = buffer_.substr(0, crlf);
                buffer_.erase(0, crlf + 2);
                
                if (!parseRequestLine(requestLine)) {
                    state_ = HttpParseState::PARSE_ERROR;
                    break;
                }
                
                state_ = HttpParseState::EXPECT_HEADERS;
            } else {
                break; // 需要更多数据
            }
        } else if (state_ == HttpParseState::EXPECT_HEADERS) {
            size_t crlf = buffer_.find("\r\n");
            if (crlf != std::string::npos) {
                if (crlf == 0) {
                    // 空行，头部结束
                    buffer_.erase(0, 2);
                    
                    // 检查是否有请求体
                    std::string contentLengthStr = request_.getHeader("Content-Length");
                    if (!contentLengthStr.empty()) {
                        contentLength_ = std::stoul(contentLengthStr);
                        if (contentLength_ > 0) {
                            state_ = HttpParseState::EXPECT_BODY;
                        } else {
                            state_ = HttpParseState::GOT_ALL;
                        }
                    } else {
                        state_ = HttpParseState::GOT_ALL;
                    }
                } else {
                    std::string headerLine = buffer_.substr(0, crlf);
                    buffer_.erase(0, crlf + 2);
                    
                    if (!parseHeader(headerLine)) {
                        state_ = HttpParseState::PARSE_ERROR;
                        break;
                    }
                }
            } else {
                break; // 需要更多数据
            }
        } else if (state_ == HttpParseState::EXPECT_BODY) {
            if (buffer_.size() >= contentLength_) {
                parseBody(buffer_.data(), contentLength_);
                buffer_.erase(0, contentLength_);
                state_ = HttpParseState::GOT_ALL;
            } else {
                break; // 需要更多数据
            }
        }
    }
    
    return state_;
}

void HttpParser::reset() {
    state_ = HttpParseState::EXPECT_REQUEST_LINE;
    request_.reset();
    buffer_.clear();
    contentLength_ = 0;
}

bool HttpParser::parseRequestLine(const std::string& line) {
    std::istringstream iss(line);
    std::string method, path, version;
    
    if (!(iss >> method >> path >> version)) {
        return false;
    }
    
    request_.setMethod(HttpRequest::stringToMethod(method));
    
    // 解析路径和查询字符串
    size_t questionPos = path.find('?');
    if (questionPos != std::string::npos) {
        request_.setPath(path.substr(0, questionPos));
        request_.setQuery(path.substr(questionPos + 1));
    } else {
        request_.setPath(path);
    }
    
    request_.setVersion(HttpRequest::stringToVersion(version));
    
    return request_.method() != HttpMethod::UNKNOWN && 
           request_.version() != HttpVersion::UNKNOWN;
}

bool HttpParser::parseHeader(const std::string& line) {
    auto headerPair = splitHeader(line);
    if (headerPair.first.empty()) {
        return false;
    }
    
    request_.addHeader(headerPair.first, headerPair.second);
    return true;
}

void HttpParser::parseBody(const char* data, size_t len) {
    request_.setBody(std::string(data, len));
}

std::string HttpParser::trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t");
    if (first == std::string::npos) return "";
    
    size_t last = str.find_last_not_of(" \t\r");
    return str.substr(first, (last - first + 1));
}

std::pair<std::string, std::string> HttpParser::splitHeader(const std::string& header) {
    size_t colonPos = header.find(':');
    if (colonPos == std::string::npos) {
        return {"", ""};
    }
    
    std::string key = trim(header.substr(0, colonPos));
    std::string value = trim(header.substr(colonPos + 1));
    
    return {key, value};
}

} // namespace http
} // namespace llama