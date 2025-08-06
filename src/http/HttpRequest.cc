#include "http/HttpRequest.h"
#include <algorithm>

namespace llama {
namespace http {

HttpRequest::HttpRequest()
    : method_(HttpMethod::UNKNOWN)
    , version_(HttpVersion::UNKNOWN)
{
}

void HttpRequest::addHeader(const std::string& key, const std::string& value) {
    headers_[key] = value;
}

std::string HttpRequest::getHeader(const std::string& key) const {
    auto it = headers_.find(key);
    return it != headers_.end() ? it->second : "";
}

void HttpRequest::reset() {
    method_ = HttpMethod::UNKNOWN;
    path_.clear();
    query_.clear();
    version_ = HttpVersion::UNKNOWN;
    headers_.clear();
    body_.clear();
}

std::string HttpRequest::methodString() const {
    switch (method_) {
        case HttpMethod::GET: return "GET";
        case HttpMethod::POST: return "POST";
        case HttpMethod::PUT: return "PUT";
        case HttpMethod::DELETE: return "DELETE";
        case HttpMethod::OPTIONS: return "OPTIONS";
        case HttpMethod::HEAD: return "HEAD";
        case HttpMethod::PATCH: return "PATCH";
        default: return "UNKNOWN";
    }
}

std::string HttpRequest::versionString() const {
    switch (version_) {
        case HttpVersion::HTTP10: return "HTTP/1.0";
        case HttpVersion::HTTP11: return "HTTP/1.1";
        default: return "UNKNOWN";
    }
}

HttpMethod HttpRequest::stringToMethod(const std::string& method) {
    if (method == "GET") return HttpMethod::GET;
    if (method == "POST") return HttpMethod::POST;
    if (method == "PUT") return HttpMethod::PUT;
    if (method == "DELETE") return HttpMethod::DELETE;
    if (method == "OPTIONS") return HttpMethod::OPTIONS;
    if (method == "HEAD") return HttpMethod::HEAD;
    if (method == "PATCH") return HttpMethod::PATCH;
    return HttpMethod::UNKNOWN;
}

HttpVersion HttpRequest::stringToVersion(const std::string& version) {
    if (version == "HTTP/1.0") return HttpVersion::HTTP10;
    if (version == "HTTP/1.1") return HttpVersion::HTTP11;
    return HttpVersion::UNKNOWN;
}

} // namespace http
} // namespace llama