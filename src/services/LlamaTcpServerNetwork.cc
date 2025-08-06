#include "services/LlamaTcpServer.h"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <iostream>
#include <cstring>
#include <cerrno>

#define BUFFER_SIZE 4096

using namespace llama;

bool LlamaTcpServer::setupSocket() {
    // 创建套接字
    if ((server_fd_ = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        std::cerr << "❌ Socket 创建失败" << std::endl;
        return false;
    }

    int opt = 1;
    setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(config_.port);

    if (bind(server_fd_, (struct sockaddr *)&address, sizeof(address)) < 0) {
        std::cerr << "❌ 绑定端口失败" << std::endl;
        return false;
    }

    if (listen(server_fd_, 3) < 0) {
        std::cerr << "❌ 监听失败" << std::endl;
        return false;
    }

    std::cout << "🚀 LLaMA TCP 服务已启动，监听端口 " << config_.port << std::endl;
    return true;
}

int LlamaTcpServer::run() {
    is_running_ = true;
    acceptConnections();
    return 0;
}

void LlamaTcpServer::acceptConnections() {
    struct sockaddr_in address;
    int addrlen = sizeof(address);
    
    while (is_running_) {
        int new_socket = accept(server_fd_, (struct sockaddr *)&address, (socklen_t *)&addrlen);
        if (new_socket < 0) {
            std::cerr << "❌ 接受连接失败" << std::endl;
            continue;
        }

        if (!handleClientConnection(new_socket)) {
            std::cerr << "❌ 处理客户端连接失败" << std::endl;
        }

        close(new_socket);
        std::cout << "✅ 响应已发送，等待新请求..." << std::endl;
    }
}

bool LlamaTcpServer::handleClientConnection(int client_socket) {
    char buffer[BUFFER_SIZE] = {0};
    
    // 读取客户端的提示词
    memset(buffer, 0, BUFFER_SIZE);
    ssize_t bytes_read = read(client_socket, buffer, BUFFER_SIZE - 1);
    if (bytes_read <= 0) {
        std::cerr << "❌ 读取客户端数据失败或连接关闭" << std::endl;
        return false;
    }
    
    std::string prompt(buffer);
    std::cout << "📥 收到请求：" << prompt << std::endl;

    // 处理请求并获取响应
    std::string response = processQuery(prompt);
    if (response.empty()) {
        response = R"({"response": "处理请求失败", "cached": false})";
    }

    // 发送响应
    size_t total_sent = 0;
    size_t response_length = response.length();

    while (total_sent < response_length) {
        ssize_t sent = send(client_socket, response.c_str() + total_sent, 
                           response_length - total_sent, 0);
        if (sent < 0) {
            std::cerr << "❌ 发送响应失败: " << strerror(errno) << std::endl;
            return false;
        }
        total_sent += sent;
        std::cout << "📤 已发送 " << total_sent << "/" << response_length << " 字节" << std::endl;
    }

    if (total_sent == response_length) {
        std::cout << "✅ JSON响应发送完成" << std::endl;
        return true;
    } else {
        std::cout << "⚠️ JSON响应发送不完整" << std::endl;
        return false;
    }
}
