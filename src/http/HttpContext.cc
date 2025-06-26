#include "http/HttpContext.h"

namespace kama {
namespace http {

HttpContext::HttpContext()
{
}

HttpParseState HttpContext::parseRequest(const char* data, size_t len) {
    return parser_.parseRequest(data, len);
}

void HttpContext::reset() {
    parser_.reset();
}

} // namespace http
} // namespace kama