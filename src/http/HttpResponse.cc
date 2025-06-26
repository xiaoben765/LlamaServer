#include "http/HttpResponse.h"
#include <sstream>

namespace kama {
namespace http {

HttpResponse::HttpResponse(HttpVersion version)
    : version_(version)
    , statusCode_(HttpStatusCode::OK)
    , statusMessage_("OK")
{
}

void HttpResponse::setStatusCode(HttpStatusCode code) {
    statusCode_ = code;
    statusMessage_ = getStatusString(code);
}

void HttpResponse::setStatusMessage(const std::string& message) {
    statusMessage_ = message;
}

void HttpResponse::setHeader(const std::string& key, const std::string& value) {
    headers_[key] = value;
}

void HttpResponse::setContentType(const std::string& type) {
    setHeader("Content-Type", type);
}

void HttpResponse::setContentLength(size_t length) {
    setHeader("Content-Length", std::to_string(length));
}

void HttpResponse::setBody(const std::string& body) {
    body_ = body;
    setContentLength(body_.size());
}

void HttpResponse::appendBody(const std::string& content) {
    body_ += content;
    setContentLength(body_.size());
}

std::string HttpResponse::toString() const {
    std::ostringstream ss;
    
    // 状态行
    ss << (version_ == HttpVersion::HTTP10 ? "HTTP/1.0" : "HTTP/1.1")
       << " " << static_cast<int>(statusCode_)
       << " " << statusMessage_ << "\r\n";
    
    // 头部
    for (const auto& header : headers_) {
        ss << header.first << ": " << header.second << "\r\n";
    }
    
    // 空行
    ss << "\r\n";
    
    // 响应体
    ss << body_;
    
    return ss.str();
}

void HttpResponse::setJsonResponse(const std::string& json) {
    setContentType("application/json; charset=utf-8");
    setBody(json);
}

void HttpResponse::setHtmlResponse(const std::string& html) {
    setContentType("text/html; charset=utf-8");
    setBody(html);
}

void HttpResponse::setTextResponse(const std::string& text) {
    setContentType("text/plain; charset=utf-8");
    setBody(text);
}

void HttpResponse::setErrorResponse(HttpStatusCode code, const std::string& message) {
    setStatusCode(code);
    setContentType("application/json");
    
    std::ostringstream json;
    json << "{\"error\": \"" << message << "\", \"code\": " << static_cast<int>(code) << "}";
    setBody(json.str());
}

std::string HttpResponse::getStatusString(HttpStatusCode code) const {
    switch (code) {
        case HttpStatusCode::OK: return "OK";
        case HttpStatusCode::CREATED: return "Created";
        case HttpStatusCode::NO_CONTENT: return "No Content";
        case HttpStatusCode::BAD_REQUEST: return "Bad Request";
        case HttpStatusCode::UNAUTHORIZED: return "Unauthorized";
        case HttpStatusCode::FORBIDDEN: return "Forbidden";
        case HttpStatusCode::NOT_FOUND: return "Not Found";
        case HttpStatusCode::METHOD_NOT_ALLOWED: return "Method Not Allowed";
        case HttpStatusCode::INTERNAL_SERVER_ERROR: return "Internal Server Error";
        case HttpStatusCode::NOT_IMPLEMENTED: return "Not Implemented";
        case HttpStatusCode::BAD_GATEWAY: return "Bad Gateway";
        case HttpStatusCode::SERVICE_UNAVAILABLE: return "Service Unavailable";
        default: return "Unknown";
    }
}

} // namespace http
} // namespace kama