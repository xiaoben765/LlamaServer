#include "services/LlamaService.h"
#include "Logger.h"
#include <chrono>
#include <thread>
#include <nlohmann/json.hpp>
#include <regex>

namespace kama {
namespace services {

// LlamaTcpService实现

bool LlamaTcpService::checkServiceAvailable() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        LOG_ERROR << "创建套接字失败";
        return false;
    }
    
    // 设置更短的连接超时，避免长时间等待
    struct timeval tv;
    tv.tv_sec = 2;  // 2秒超时
    tv.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));
    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(serverPort_);
    
    if (inet_pton(AF_INET, serverIp_.c_str(), &server_addr.sin_addr) <= 0) {
        LOG_ERROR << "无效的IP地址: " << serverIp_;
        close(sock);
        return false;
    }
    
    // 设置非阻塞模式
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
    
    // 尝试连接
    int ret = connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr));
    
    if (ret < 0) {
        if (errno == EINPROGRESS) {
            fd_set fdset;
            FD_ZERO(&fdset);
            FD_SET(sock, &fdset);
            
            // 等待连接完成或超时
            ret = select(sock + 1, NULL, &fdset, NULL, &tv);
            
            if (ret > 0) {
                int error;
                socklen_t len = sizeof(error);
                if (getsockopt(sock, SOL_SOCKET, SO_ERROR, &error, &len) < 0 || error) {
                    LOG_ERROR << "连接LLaMA服务失败: " << strerror(error);
                    close(sock);
                    return false;
                }
                // 连接成功
            } else {
                LOG_ERROR << "连接LLaMA服务超时或错误";
                close(sock);
                return false;
            }
        } else {
            LOG_ERROR << "连接LLaMA服务出错: " << strerror(errno);
            close(sock);
            return false;
        }
    }
    
    close(sock);
    return true;
}

bool LlamaTcpService::ensureConnection() {
    if (persistent_sock_ > 0) {
        // 检查连接是否仍然有效
        char dummy;
        if (recv(persistent_sock_, &dummy, 1, MSG_PEEK | MSG_DONTWAIT) == 0) {
            // 连接已关闭
            LOG_WARN << "LLaMA服务连接已关闭，尝试重新连接";
            closeConnection();
        }
    }
    
    // 如果没有有效连接，创建一个新的
    if (persistent_sock_ <= 0) {
        persistent_sock_ = socket(AF_INET, SOCK_STREAM, 0);
        if (persistent_sock_ < 0) {
            LOG_ERROR << "创建套接字失败";
            return false;
        }
        
        struct sockaddr_in server_addr;
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(serverPort_);
        
        if (inet_pton(AF_INET, serverIp_.c_str(), &server_addr.sin_addr) <= 0) {
            LOG_ERROR << "无效的IP地址: " << serverIp_;
            close(persistent_sock_);
            persistent_sock_ = -1;
            return false;
        }
        
        // 设置非阻塞模式
        int flags = fcntl(persistent_sock_, F_GETFL, 0);
        fcntl(persistent_sock_, F_SETFL, flags | O_NONBLOCK);
        
        // 设置超时
        struct timeval tv;
        tv.tv_sec = 5;  // 5秒超时
        tv.tv_usec = 0;
        setsockopt(persistent_sock_, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
        setsockopt(persistent_sock_, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));
        
        // 尝试连接
        int ret = connect(persistent_sock_, (struct sockaddr*)&server_addr, sizeof(server_addr));
        
        if (ret < 0) {
            if (errno == EINPROGRESS) {
                fd_set fdset;
                FD_ZERO(&fdset);
                FD_SET(persistent_sock_, &fdset);
                
                // 等待连接完成或超时
                tv.tv_sec = 5;  // 5秒超时
                ret = select(persistent_sock_ + 1, NULL, &fdset, NULL, &tv);
                
                if (ret > 0) {
                    int error;
                    socklen_t len = sizeof(error);
                    if (getsockopt(persistent_sock_, SOL_SOCKET, SO_ERROR, &error, &len) < 0 || error) {
                        LOG_ERROR << "连接LLaMA服务失败: " << strerror(error);
                        closeConnection();
                        return false;
                    }
                    // 连接成功
                    LOG_INFO << "已连接到LLaMA服务";
                } else {
                    LOG_ERROR << "连接LLaMA服务超时或错误";
                    closeConnection();
                    return false;
                }
            } else {
                LOG_ERROR << "连接LLaMA服务出错: " << strerror(errno);
                closeConnection();
                return false;
            }
        }
        
        // 恢复阻塞模式
        flags = fcntl(persistent_sock_, F_GETFL, 0);
        fcntl(persistent_sock_, F_SETFL, flags & ~O_NONBLOCK);
    }
    
    return true;
}

void LlamaTcpService::closeConnection() {
    if (persistent_sock_ > 0) {
        close(persistent_sock_);
        persistent_sock_ = -1;
    }
}

void LlamaTcpService::cleanInvalidUtf8(std::string& str) {
    // 简单替换无效UTF-8字符
    for (size_t i = 0; i < str.length(); ++i) {
        if ((str[i] & 0x80) && !(str[i] & 0x40)) {
            // 非法UTF-8序列
            str[i] = '?';
        }
    }
}

void LlamaTcpService::replaceAll(std::string& str, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = str.find(from, pos)) != std::string::npos) {
        str.replace(pos, from.length(), to);
        pos += to.length();
    }
}

// LlamaMockService实现

std::string LlamaMockService::query(const std::string& prompt) {
    // 记录API调用
    LOG_INFO << "LlamaMock调用，提示词: " << prompt.substr(0, 100) << "...";
    
    // 简单模拟处理延迟
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    
    // 根据提示词生成模拟响应
    std::string response;
    
    if (prompt.find("hello") != std::string::npos || 
        prompt.find("你好") != std::string::npos) {
        response = "你好！我是一个模拟的LLaMA助手。我可以回答简单的问题，但这只是一个模拟响应。";
    }
    else if (prompt.find("weather") != std::string::npos || 
             prompt.find("天气") != std::string::npos) {
        response = "我是一个模拟服务，无法获取实时天气信息。在实际应用中，这里会连接到真实的LLaMA模型来获取回答。";
    }
    else if (prompt.find("time") != std::string::npos || 
             prompt.find("时间") != std::string::npos) {
        // 获取当前时间
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        std::ostringstream oss;
        oss << std::ctime(&time_t_now);
        response = "现在的系统时间是: " + oss.str() + "（这是从模拟服务中生成的回复）";
    }
    else if (prompt.find("code") != std::string::npos || 
             prompt.find("代码") != std::string::npos) {
        response = "这是一个简单的C++函数示例：\n\n```cpp\nvoid sayHello() {\n    std::cout << \"Hello, World!\" << std::endl;\n}\n```\n\n这只是一个模拟响应。实际的LLaMA模型能够生成更复杂的代码。";
    }
    else {
        response = "这是来自模拟LLaMA服务的回复。您的提问是：\"" + 
                   (prompt.length() > 100 ? prompt.substr(0, 100) + "..." : prompt) + 
                   "\"。在实际应用中，这里会连接到真实的LLaMA模型来获取更有意义的回答。";
    }
    
    return response;
}

} // namespace services
} // namespace kama
