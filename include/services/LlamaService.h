#pragma once

#include "services/ILlamaService.h"
#include "Logger.h"
#include <string>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <chrono>
#include <thread>
#include <cstring>
#include <regex>

namespace kama {
namespace services {

/**
 * @brief LLaMA TCP客户端服务实现
 * 
 * 通过TCP协议连接到LLaMA服务
 */
class LlamaTcpService : public ILlamaService {
public:
    /**
     * @brief 构造函数
     * 
     * @param modelPath 模型路径（用于记录）
     * @param serverIp 服务器IP地址
     * @param serverPort 服务器端口
     */
    LlamaTcpService(
        const std::string& modelPath, 
        const std::string& serverIp = "127.0.0.1", 
        int serverPort = 8899
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
    
    ~LlamaTcpService() override {
        if (persistent_sock_ > 0) {
            close(persistent_sock_);
        }
    }
    
    bool isAvailable() const override {
        return available_;
    }
    
    std::string query(const std::string& message) override {
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
    
    bool resetConnection() override {
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
    
private:
    bool checkServiceAvailable() {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            LOG_ERROR << "创建套接字失败";
            return false;
        }
        
        // 设置更短的连接超时，避免长时间等待
        struct timeval tv;
        tv.tv_sec = 1;  // 1秒超时
        tv.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof tv);
        
        // 连接服务器
        struct sockaddr_in serv_addr;
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(serverPort_);
        inet_pton(AF_INET, serverIp_.c_str(), &serv_addr.sin_addr);
        
        bool success = connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) >= 0;
        if (!success) {
            LOG_ERROR << "LLaMA 服务连接失败: " << strerror(errno);
        }
        close(sock);
        return success;
    }

    bool ensureConnection() {
        if (persistent_sock_ > 0) {
            // 测试连接是否还活着
            char test_buf[1];
            int result = recv(persistent_sock_, test_buf, 1, MSG_PEEK | MSG_DONTWAIT);
            if (result >= 0 || (result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))) {
                return true; // 连接正常
            }
            
            LOG_INFO << "连接已断开，尝试重新连接";
            closeConnection();
        }
        
        // 创建新连接
        persistent_sock_ = socket(AF_INET, SOCK_STREAM, 0);
        if (persistent_sock_ < 0) {
            LOG_ERROR << "无法创建套接字";
            return false;
        }
        
        // 设置超时
        struct timeval tv;
        tv.tv_sec = 30;
        tv.tv_usec = 0;
        setsockopt(persistent_sock_, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
        setsockopt(persistent_sock_, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof tv);
        
        // 连接服务器
        struct sockaddr_in serv_addr;
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(serverPort_);
        inet_pton(AF_INET, serverIp_.c_str(), &serv_addr.sin_addr);
        
        if (connect(persistent_sock_, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
            LOG_ERROR << "连接到LLaMA服务器失败: " << strerror(errno);
            closeConnection();
            available_ = false;
            return false;
        }
        
        // 设置非阻塞模式
        int flags = fcntl(persistent_sock_, F_GETFL, 0);
        fcntl(persistent_sock_, F_SETFL, flags | O_NONBLOCK);
        
        available_ = true;
        LOG_INFO << "成功建立到LLaMA服务的新连接";
        return true;
    }
    
    void closeConnection() {
        if (persistent_sock_ > 0) {
            close(persistent_sock_);
            persistent_sock_ = -1;
        }
    }
    
    // 辅助函数：清理非UTF-8字符
    void cleanInvalidUtf8(std::string& str) {
        std::string result;
        result.reserve(str.size());
        
        for (size_t i = 0; i < str.size(); ) {
            unsigned char c = str[i];
            if (c < 0x80) {  // ASCII字符
                result.push_back(c);
                i++;
            } else if ((c & 0xE0) == 0xC0) {  // 2字节UTF-8
                if (i + 1 < str.size() && (str[i+1] & 0xC0) == 0x80) {
                    result.push_back(c);
                    result.push_back(str[i+1]);
                    i += 2;
                } else {
                    // 无效UTF-8
                    result.push_back('?');
                    i++;
                }
            } else if ((c & 0xF0) == 0xE0) {  // 3字节UTF-8
                if (i + 2 < str.size() && 
                    (str[i+1] & 0xC0) == 0x80 && 
                    (str[i+2] & 0xC0) == 0x80) {
                    result.push_back(c);
                    result.push_back(str[i+1]);
                    result.push_back(str[i+2]);
                    i += 3;
                } else {
                    // 无效UTF-8
                    result.push_back('?');
                    i++;
                }
            } else if ((c & 0xF8) == 0xF0) {  // 4字节UTF-8
                if (i + 3 < str.size() && 
                    (str[i+1] & 0xC0) == 0x80 && 
                    (str[i+2] & 0xC0) == 0x80 && 
                    (str[i+3] & 0xC0) == 0x80) {
                    result.push_back(c);
                    result.push_back(str[i+1]);
                    result.push_back(str[i+2]);
                    result.push_back(str[i+3]);
                    i += 4;
                } else {
                    // 无效UTF-8
                    result.push_back('?');
                    i++;
                }
            } else {
                // 无效UTF-8起始字节
                result.push_back('?');
                i++;
            }
        }
        
        str = result;
    }

    // 辅助函数：字符串替换
    void replaceAll(std::string& str, const std::string& from, const std::string& to) {
        size_t pos = 0;
        while ((pos = str.find(from, pos)) != std::string::npos) {
            str.replace(pos, from.length(), to);
            pos += to.length();
        }
    }

    std::string modelPath_;
    std::string serverIp_;
    int serverPort_;
    bool available_;
    int persistent_sock_; // 持久连接套接字
};

/**
 * @brief LLaMA Mock服务实现
 * 
 * 用于测试，返回预定义的响应
 */
class LlamaMockService : public ILlamaService {
public:
    LlamaMockService(bool available = true) : available_(available) {}
    
    bool isAvailable() const override { return available_; }
    
    std::string query(const std::string& prompt) override {
        if (!available_) {
            return "服务不可用";
        }
        
        return "这是一个模拟的LLaMA响应。您的问题是: " + prompt;
    }
    
    bool resetConnection() override { 
        return available_; 
    }
    
private:
    bool available_;
};

} // namespace services
} // namespace kama
