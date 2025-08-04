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
        tv.tv_sec = 120; // 120秒超时，适应LLaMA推理时间
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
                    
                    if (elapsed > 60) { // 60秒无数据则超时
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
            // 检查JSON格式
            LOG_INFO << "检查JSON开始字符: " << (response[0] == '{' ? "正确" : "错误");
            LOG_INFO << "检查JSON结束字符: " << (response[response.length()-1] == '}' ? "正确" : "错误");
        }
        
        // 尝试解析JSON
        try {
            // 深度处理可能的编码问题
            cleanInvalidUtf8(response);
            
            // 重新检查JSON格式
            LOG_INFO << "清理后的JSON格式检查";
            LOG_INFO << "清理后的响应长度: " << response.length();
            if (!response.empty()) {
                LOG_INFO << "清理后响应前100字符: " << response.substr(0, 100);
                LOG_INFO << "清理后响应最后100字符: " << (response.length() > 100 ? 
                            response.substr(response.length() - 100) : response);
            }
            
            // 确保JSON格式正确
            if (response[0] != '{' || response[response.length()-1] != '}') {
                LOG_WARN << "JSON格式不正确，尝试修复";
                // 确保以{开始，以}结束
                if (response[0] != '{') {
                    size_t pos = response.find('{');
                    if (pos != std::string::npos) {
                        response = response.substr(pos);
                    } else {
                        response = "{" + response;
                    }
                }
                
                if (response[response.length()-1] != '}') {
                    size_t pos = response.rfind('}');
                    if (pos != std::string::npos) {
                        response = response.substr(0, pos+1);
                    } else {
                        response = response + "}";
                    }
                }
            }
            
            try {
                nlohmann::json parsed = nlohmann::json::parse(response);
                if (parsed.contains("response")) {
                    std::string ai_response = parsed["response"].get<std::string>();
                    // 处理响应中的无效UTF-8字符
                    cleanInvalidUtf8(ai_response);
                    LOG_INFO << "成功解析JSON，获取到response字段";
                    return ai_response;
                } else {
                    LOG_ERROR << "JSON中缺少response字段";
                    return "响应格式错误，请重试";
                }
            } catch (const nlohmann::json::parse_error& e) {
                LOG_ERROR << "JSON解析失败: " << e.what();
                LOG_ERROR << "解析失败的原始数据长度: " << response.length();
                LOG_ERROR << "原始数据前200字符: " << response.substr(0, 200);
                LOG_ERROR << "原始数据最后200字符: " << (response.length() > 200 ? 
                            response.substr(response.length() - 200) : response);
                
                // 尝试手动解析JSON
                LOG_INFO << "尝试手动提取JSON内容";
                std::regex responseRegex("\"response\"\\s*:\\s*\"(.*?)\"");
                std::smatch match;
                std::string resp_copy = response;
                
                // 替换所有转义双引号，以便正则表达式能正确匹配
                replaceAll(resp_copy, "\\\"", "__QUOTE__");
                
                if (std::regex_search(resp_copy, match, responseRegex) && match.size() > 1) {
                    LOG_INFO << "通过正则提取到response内容";
                    std::string extracted = match[1].str();
                    // 还原转义字符
                    replaceAll(extracted, "__QUOTE__", "\"");
                    replaceAll(extracted, "\\n", "\n");
                    replaceAll(extracted, "\\\\", "\\");
                    
                    // 清理提取的内容
                    cleanInvalidUtf8(extracted);
                    return extracted;
                }
                
                // 如果完全无法解析，尝试提取任何有用的内容
                std::string fallback_response;
                bool in_ascii_segment = false;
                int ascii_count = 0;
                
                for (size_t i = 0; i < response.length(); i++) {
                    char c = response[i];
                    if (c >= 32 && c <= 126) { // ASCII可打印字符
                        if (!in_ascii_segment) {
                            in_ascii_segment = true;
                        }
                        fallback_response += c;
                        ascii_count++;
                    } else if (in_ascii_segment) {
                        fallback_response += ' ';
                        in_ascii_segment = false;
                    }
                }
                
                if (ascii_count > 20) {
                    LOG_INFO << "从响应中提取了ASCII文本内容";
                    return fallback_response;
                }
                
                // 最后的回退选项
                return "无法解析LLaMA响应。请稍后再试。";
            }
        } catch (const std::exception& e) {
            LOG_ERROR << "JSON处理异常: " << e.what();
            return "处理响应时发生错误，请重试";
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
        tv.tv_sec = 120;  // 120秒超时，适应LLaMA推理时间
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
    // 更彻底地清理无效的UTF-8字符
    std::string result;
    result.reserve(str.length());
    
    for (size_t i = 0; i < str.length(); ) {
        unsigned char c = static_cast<unsigned char>(str[i]);
        
        if (c < 0x80) {
            // ASCII字符，直接复制
            result.push_back(str[i]);
            i++;
        } else if (c >= 0xC0 && c <= 0xDF) {
            // 2字节UTF-8序列
            if (i + 1 < str.length() && (static_cast<unsigned char>(str[i+1]) & 0xC0) == 0x80) {
                result.push_back(str[i]);
                result.push_back(str[i+1]);
                i += 2;
            } else {
                // 无效序列，替换
                result.push_back('?');
                i++;
            }
        } else if (c >= 0xE0 && c <= 0xEF) {
            // 3字节UTF-8序列
            if (i + 2 < str.length() && 
                (static_cast<unsigned char>(str[i+1]) & 0xC0) == 0x80 && 
                (static_cast<unsigned char>(str[i+2]) & 0xC0) == 0x80) {
                result.push_back(str[i]);
                result.push_back(str[i+1]);
                result.push_back(str[i+2]);
                i += 3;
            } else {
                // 无效序列，替换
                result.push_back('?');
                i++;
            }
        } else if (c >= 0xF0 && c <= 0xF7) {
            // 4字节UTF-8序列
            if (i + 3 < str.length() && 
                (static_cast<unsigned char>(str[i+1]) & 0xC0) == 0x80 && 
                (static_cast<unsigned char>(str[i+2]) & 0xC0) == 0x80 && 
                (static_cast<unsigned char>(str[i+3]) & 0xC0) == 0x80) {
                result.push_back(str[i]);
                result.push_back(str[i+1]);
                result.push_back(str[i+2]);
                result.push_back(str[i+3]);
                i += 4;
            } else {
                // 无效序列，替换
                result.push_back('?');
                i++;
            }
        } else {
            // 无效字节，替换
            result.push_back('?');
            i++;
        }
    }
    
    str = result;
}

void LlamaTcpService::replaceAll(std::string& str, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = str.find(from, pos)) != std::string::npos) {
        str.replace(pos, from.length(), to);
        pos += to.length();
    }
}



} // namespace services
} // namespace kama
