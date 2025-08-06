#pragma once

#include "HttpParser.h"
#include <memory>

namespace llama {
namespace http {

class HttpContext {
public:
    HttpContext();
    ~HttpContext() = default;

    HttpParseState parseRequest(const char* data, size_t len);
    
    const HttpRequest& request() const { return parser_.request(); }
    HttpRequest& request() { return parser_.request(); }
    
    bool hasError() const { return parser_.hasError(); }
    bool isComplete() const { return parser_.isComplete(); }
    
    void reset();

private:
    HttpParser parser_;
};

} // namespace http
} // namespace llama