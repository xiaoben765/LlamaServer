#pragma once

#include "services/ILlamaService.h"
#include <string>
#include <nlohmann/json.hpp>
#include <regex>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

namespace llama {
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
    );
    
    ~LlamaTcpService() override;
    bool isAvailable() const override;
    std::string query(const std::string& message) override;
    bool resetConnection() override;

private:
    bool checkServiceAvailable();
    bool ensureConnection();
    void closeConnection();
    void cleanInvalidUtf8(std::string& str);
    void replaceAll(std::string& str, const std::string& from, const std::string& to);

    std::string modelPath_;
    std::string serverIp_;
    int serverPort_;
    bool available_;
    int persistent_sock_; // 持久连接套接字
};

} // namespace services
} // namespace llama
