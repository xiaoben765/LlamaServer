#include "services/LlamaService.h"
#include "Logger.h"
#include <chrono>
#include <thread>
#include <nlohmann/json.hpp>
#include <regex>
#include <sstream>
#include <cstring>
#include <ctime>
#include <errno.h>

namespace kama {
namespace services {

// LlamaTcpService实现
LlamaTcpService::LlamaTcpService(
    const std::string& modelPath, 
    const std::string& serverIp, 
    int serverPort
) : 
    modelPath_(modelPath),
    serverIp_(serverIp),
    serverPort_(serverPort),
    persistent_sock_(-1)
{
    // 初始化时检查服务可用性
    available_ = checkServiceAvailable();
    if (available_) {
        LOG_INFO << "LLaMA 服务连接成功：" << serverIp_ << ":" << serverPort_;
    } else {
        LOG_ERROR << "LLaMA 服务不可用：" << serverIp_ << ":" << serverPort_;
    }
}
    
LlamaTcpService::~LlamaTcpService() {
    if (persistent_sock_ > 0) {
        close(persistent_sock_);
    }
}

bool LlamaTcpService::isAvailable() const {
    return available_;
}

std::string LlamaTcpService::query(const std::string& message) {
    // 尝试重新连接
    if (!ensureConnection()) {
        LOG_ERROR << "无法连接到 LLaMA 服务";
        return "服务暂时不可用，请稍后重试";
    }
    
    try {
        // 发送请求
        if (send(persistent_sock_, message.c_str(), message.length(), 0) < 0) {
            LOG_ERROR << "发送请求失败: " << strerror(errno);
            closeConnection();
            return "发送请求失败，请重试";
        }
        
        // 接收响应 - 增强版
        std::string response;
        char buffer[8192]; // 更大的缓冲区
        ssize_t bytes_read;
        
        auto start_time = std::chrono::steady_clock::now();
        bool reading = true;
        
        // 设置接收超时
        struct timeval tv;
        tv.tv_sec = 30; // 30秒超时
        tv.tv_usec = 0;
        setsockopt(persistent_sock_, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
        
        while (reading) {
            bytes_read = recv(persistent_sock_, buffer, sizeof(buffer) - 1, 0);
            
            if (bytes_read > 0) {
                buffer[bytes_read] = '\0';
                response += buffer;
                
                // 尝试判断是否接收完整 - 检查JSON结束
                if (response.find("}") != std::string::npos && 
                    response.rfind("}") == response.length() - 1) {
                    break;
                }
                
            } else if (bytes_read == 0) {
                // 连接关闭
                LOG_INFO << "LLaMA 服务连接已关闭";
                closeConnection();
                break;
            } else {
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    LOG_ERROR << "接收数据错误: " << strerror(errno);
                    closeConnection();
                    break;
                } else {
                    // 等待更多数据
                    auto now = std::chrono::steady_clock::now();
                    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();
                    
                    if (elapsed > 20) { // 20秒无数据则超时
                        LOG_WARN << "接收数据超时";
                        break;
                    }
                    
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            }
        }
        
        // 详细记录接收到的数据
        LOG_INFO << "接收到原始响应，长度: " << response.length();
        if (!response.empty()) {
            LOG_INFO << "响应前100字符: " << response.substr(0, 100);
            LOG_INFO << "响应最后100字符: " << (response.length() > 100 ? 
                        response.substr(response.length() - 100) : response);
        }
        
        // 尝试解析JSON
        try {
            // 处理可能的编码问题
            cleanInvalidUtf8(response);
            
            nlohmann::json parsed = nlohmann::json::parse(response);
            if (parsed.contains("response")) {
                std::string ai_response = parsed["response"].get<std::string>();
                LOG_INFO << "成功解析JSON，获取到response字段";
                return ai_response;
            } else {
                LOG_ERROR << "JSON中缺少response字段";
                return "响应格式错误，请重试";
            }
        } catch (const nlohmann::json::parse_error& e) {
            LOG_ERROR << "JSON解析失败: " << e.what();
            
            // 失败后尝试正则表达式提取
            std::regex responseRegex("\"response\"\\s*:\\s*\"(.*?)\"\\s*,");
            std::smatch match;
            if (std::regex_search(response, match, responseRegex) && match.size() > 1) {
                LOG_INFO << "通过正则提取到response内容";
                std::string extracted = match[1].str();
                // 处理转义字符
                replaceAll(extracted, "\\n", "\n");
                replaceAll(extracted, "\\\"", "\"");
                replaceAll(extracted, "\\\\", "\\");
                return extracted;
            }
            
            // 如果完全无法解析，返回可能有用的内容
            if (response.length() > 20) {
                LOG_INFO << "返回未解析的响应内容";
                return "解析错误，原始内容：" + response.substr(0, 1000);
            }
            return "无法解析服务器响应，请重试";
        }
    } catch (const std::exception& e) {
        LOG_ERROR << "处理请求异常: " << e.what();
        return "处理请求时发生错误，请重试";
    }
}

bool LlamaTcpService::resetConnection() {
    LOG_INFO << "重置与LLaMA服务的连接";
    closeConnection();  // 关闭现有连接
    available_ = checkServiceAvailable();  // 重新检查可用性
    if (available_) {
        LOG_INFO << "LLaMA 服务重新连接成功";
    } else {
        LOG_ERROR << "LLaMA 服务重新连接失败";
    }
    return available_;
}

bool LlamaTcpService::checkServiceAvailable() {
    std::cout << "检查LLaMA服务可用性: " << serverIp_ << ":" << serverPort_ << std::endl;
    
    // 创建套接字
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        std::cerr << "创建套接字失败: " << strerror(errno) << std::endl;
        LOG_ERROR << "创建套接字失败: " << strerror(errno);
        return false;
    }
    std::cout << "套接字创建成功" << std::endl;
    
    // 设置更短的连接超时，避免长时间等待
    struct timeval tv;
    tv.tv_sec = 2;  // 2秒超时
    tv.tv_usec = 0;
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv)) < 0) {
        std::cerr << "设置接收超时失败: " << strerror(errno) << std::endl;
    }
    if (setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv)) < 0) {
        std::cerr << "设置发送超时失败: " << strerror(errno) << std::endl;
    }
    
    // 准备服务器地址
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(serverPort_);
    
    // 转换IP地址
    std::cout << "转换IP地址: " << serverIp_ << std::endl;
    if (inet_pton(AF_INET, serverIp_.c_str(), &server_addr.sin_addr) <= 0) {
        std::cerr << "无效的IP地址: " << serverIp_ << " - " << strerror(errno) << std::endl;
        LOG_ERROR << "无效的IP地址: " << serverIp_ << " - " << strerror(errno);
        close(sock);
        return false;
    }
    
    // 设置非阻塞模式
    std::cout << "设置非阻塞模式" << std::endl;
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags < 0) {
        std::cerr << "获取套接字标志失败: " << strerror(errno) << std::endl;
        LOG_ERROR << "获取套接字标志失败: " << strerror(errno);
        close(sock);
        return false;
    }
    
    if (fcntl(sock, F_SETFL, flags | O_NONBLOCK) < 0) {
        std::cerr << "设置非阻塞模式失败: " << strerror(errno) << std::endl;
        LOG_ERROR << "设置非阻塞模式失败: " << strerror(errno);
        close(sock);
        return false;
    }
    
    // 尝试连接
    std::cout << "尝试连接到: " << serverIp_ << ":" << serverPort_ << std::endl;
    int ret = connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr));
    
    if (ret < 0) {
        if (errno == EINPROGRESS) {
            std::cout << "连接进行中，等待完成..." << std::endl;
            fd_set fdset;
            FD_ZERO(&fdset);
            FD_SET(sock, &fdset);
            
            // 等待连接完成或超时
            ret = select(sock + 1, NULL, &fdset, NULL, &tv);
            std::cout << "select返回值: " << ret << std::endl;
            
            if (ret > 0) {
                int error;
                socklen_t len = sizeof(error);
                if (getsockopt(sock, SOL_SOCKET, SO_ERROR, &error, &len) < 0 || error) {
                    std::cerr << "连接LLaMA服务失败: " << strerror(error) << std::endl;
                    LOG_ERROR << "连接LLaMA服务失败: " << strerror(error);
                    close(sock);
                    return false;
                }
                // 连接成功
                std::cout << "连接成功！" << std::endl;
            } else {
                std::cerr << "连接LLaMA服务超时或错误: " << strerror(errno) << std::endl;
                LOG_ERROR << "连接LLaMA服务超时或错误: " << strerror(errno);
                close(sock);
                return false;
            }
        } else {
            std::cerr << "连接LLaMA服务出错: " << strerror(errno) << std::endl;
            LOG_ERROR << "连接LLaMA服务出错: " << strerror(errno);
            close(sock);
            return false;
        }
    } else {
        std::cout << "连接立即成功！" << std::endl;
    }
    
    std::cout << "关闭测试连接" << std::endl;
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
LlamaMockService::LlamaMockService(bool available) : available_(available) {
    LOG_INFO << "创建LLaMA模拟服务，状态: " << (available_ ? "可用" : "不可用");
}

bool LlamaMockService::isAvailable() const {
    return available_;
}

std::string LlamaMockService::query(const std::string& prompt) {
    // 记录API调用
    LOG_INFO << "LlamaMock调用，提示词: " << prompt.substr(0, 100) << "...";
    
    if (!available_) {
        return "服务不可用";
    }
    
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

bool LlamaMockService::resetConnection() {
    LOG_INFO << "重置LLaMA模拟服务连接";
    return available_;
}

} // namespace services
} // namespace kama
