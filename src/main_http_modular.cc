#include "LlamaHttpApplication.h"
#include "AsyncLogging.h"
#include "Logger.h"
#include "ConfigManager.h"
#include <iostream>
#include <memory>
#include <thread>
#include <chrono>
#include <arpa/inet.h>
#include <cstring>

using namespace llama;
using namespace llama::http;

// 异步日志
AsyncLogging* g_asyncLog = nullptr;
void asyncOutput(const char* msg, int len) {
    if (g_asyncLog) {
        g_asyncLog->append(msg, len);
    }
}

int main(int argc, char* argv[]) {
    try {
        std::cout << "开始初始化模块化HTTP服务器..." << std::endl;
        
        // 设置日志
        std::cout << "初始化日志系统..." << std::endl;
        AsyncLogging log("logs/llama_http_server", 1000 * 1000);
        log.start();
        g_asyncLog = &log;
        Logger::setOutput(asyncOutput);
        std::cout << "日志系统初始化完成" << std::endl;
        
        // 创建事件循环
        std::cout << "创建事件循环..." << std::endl;
        EventLoop loop;
        std::cout << "事件循环创建完成" << std::endl;
        
        // 检查端口是否已被占用
        int port = 8080;
        bool usePort = false;
        
        // 检查命令行参数是否指定端口
        for (int i = 1; i < argc; i++) {
            if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
                port = std::atoi(argv[i+1]);
                std::cout << "使用命令行指定端口: " << port << std::endl;
                usePort = true;
                break;
            }
        }
        
        if (!usePort) {
            try {
                auto& config = ConfigManager::instance();
                config.initialize();
                port = config.get<int>("server.http_port", 8081);
                std::cout << "使用配置文件指定端口: " << port << std::endl;
            } catch (const std::exception& e) {
                std::cerr << "读取配置端口时出错: " << e.what() << ", 使用默认端口8080" << std::endl;
            }
        }
        
        // 检查端口是否已被占用
        int sockfd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) {
            std::cerr << "创建检测socket失败: " << strerror(errno) << std::endl;
        } else {
            int optval = 1;
            ::setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
            
            struct sockaddr_in addr;
            memset(&addr, 0, sizeof(addr));
            addr.sin_family = AF_INET;
            addr.sin_port = htons(port);
            addr.sin_addr.s_addr = INADDR_ANY;
            
            if (::bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
                std::cerr << "警告: 端口 " << port << " 已被占用: " << strerror(errno) << std::endl;
                std::cerr << "服务可能已在运行，或其他程序占用了该端口" << std::endl;
                std::cerr << "请确保端口" << port << "未被占用，或在配置文件中设置其他端口" << std::endl;
                return 1;
            }
            ::close(sockfd);
        }
        
        // 服务器绑定地址
        std::cout << "设置监听地址..." << std::endl;
        InetAddress listenAddr(port, "0.0.0.0");
        std::cout << "监听地址设置完成: " << port << " (0.0.0.0)" << std::endl;
        
        // 初始化应用
        std::cout << "初始化应用..." << std::endl;
        std::cout << "静态文件根目录: ./static" << std::endl;
        LlamaHttpApplication app(&loop, listenAddr, "./static");
        std::cout << "应用初始化完成" << std::endl;
        
        // 启动服务器
        std::cout << "启动服务器..." << std::endl;
        app.start();
        std::cout << "服务器启动完成" << std::endl;
        
        // 健康检查
        std::thread healthCheck([port]() {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            std::cout << "执行健康检查..." << std::endl;
            
            int sockfd = ::socket(AF_INET, SOCK_STREAM, 0);
            if (sockfd >= 0) {
                struct sockaddr_in addr;
                memset(&addr, 0, sizeof(addr));
                addr.sin_family = AF_INET;
                addr.sin_port = htons(port);
                addr.sin_addr.s_addr = inet_addr("127.0.0.1");
                
                if (::connect(sockfd, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
                    std::cout << "✅ 健康检查通过，服务器响应正常" << std::endl;
                } else {
                    std::cout << "❌ 健康检查失败，服务器无响应" << std::endl;
                }
                ::close(sockfd);
            }
        });
        healthCheck.detach();
        
        // 运行事件循环
        std::cout << "进入事件循环..." << std::endl;
        std::cout << "HTTP服务器已启动，可以接受请求" << std::endl;
        loop.loop();
        
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "程序异常: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "未知异常，程序崩溃" << std::endl;
        return 1;
    }
}
