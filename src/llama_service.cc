#include <iostream>
#include <string>
#include <cstdio>
#include <thread>
#include <mutex>
#include <atomic>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <signal.h>
#include <functional>
#include <vector>
#include <cstring>
#include "Thread.h"
#include "CurrentThread.h"

class LlamaServer {
public:
    LlamaServer(const std::string& model_path, int port = 8899, bool use_gpu = false, int gpu_layers = 0) 
        : model_path_(model_path), port_(port), running_(true), use_gpu_(use_gpu), gpu_layers_(gpu_layers) {
        
        setupLlamaProcess();
    }

    ~LlamaServer() {
        // 关闭资源
        cleanup();
    }

    void start() {
        // 创建套接字
        server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd_ < 0) {
            std::cerr << "套接字创建失败" << std::endl;
            return;
        }

        // 设置地址重用
        int opt = 1;
        if (setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
            std::cerr << "设置套接字选项失败" << std::endl;
            return;
        }

        // 绑定地址
        struct sockaddr_in address;
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(port_);

        if (bind(server_fd_, (struct sockaddr *)&address, sizeof(address)) < 0) {
            std::cerr << "绑定失败" << std::endl;
            return;
        }

        // 监听连接
        if (listen(server_fd_, 10) < 0) {
            std::cerr << "监听失败" << std::endl;
            return;
        }

        std::cout << "LLaMA 服务已启动，监听端口 " << port_ << (use_gpu_ ? " (GPU)" : " (CPU)") << std::endl;
        
        // 接受连接并处理请求
        acceptConnections();
    }

private:
    void setupLlamaProcess() {
        // 设置输入/输出管道
        std::string output_file = "/tmp/llama_output_" + std::to_string(getpid()) + ".txt";
        
        // 创建空输出文件
        FILE* create_file = fopen(output_file.c_str(), "w");
        if (create_file) {
            fclose(create_file);
        }
        
        // 启动LLaMA进程
        // 
        std::string cmd = "/home/shl203/llama.cpp/build/bin/main -m " + model_path_ +
                        " --interactive --color --threads 32 --n_batch 8 --ctx_size 4096";

        if (use_gpu_) {
            cmd += " --n-gpu-layers " + std::to_string(gpu_layers_) +
                " --n-batch 256 --threads 32 --gpu-memory 24576";
        }

        std::cout << "启动 LLaMA 命令: " << cmd << std::endl;
                  
        llama_process_ = popen(cmd.c_str(), "r+");
        if (!llama_process_) {
            std::cerr << "无法启动LLaMA进程" << std::endl;
            throw std::runtime_error("启动LLaMA进程失败");
        }
        
        
        // 给进程启动的时间
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }

    void acceptConnections() {
        int addrlen = sizeof(struct sockaddr_in);
        struct sockaddr_in address;

        while (running_) {
            int client_fd = accept(server_fd_, (struct sockaddr *)&address, (socklen_t*)&addrlen);
            if (client_fd < 0) {
                std::cerr << "接受连接失败" << std::endl;
                continue;
            }

            // 创建新线程处理客户端请求
            std::thread client_thread(&LlamaServer::handleClient, this, client_fd);
            client_thread.detach();
        }
    }

    void handleClient(int client_fd) {
        char buffer[4096] = {0};
        std::string response;
        
        // 读取客户端请求
        int bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);
        if (bytes_read > 0) {
            buffer[bytes_read] = '\0';
            std::string query = buffer;
            bool is_streaming = false;

            // 检查是否是流请求
            if (query.substr(0, 7) == "STREAM:") {
                is_streaming = true;
                query = query.substr(7); // 移除前缀
            }

            // 处理请求，根据 is_streaming 决定是否使用流式响应
            response = processLlamaQuery(query);
            
            // 发送响应
            send(client_fd, response.c_str(), response.length(), 0);
        }
        
        // 关闭连接
        close(client_fd);
    }

    std::string processLlamaQuery(const std::string& query) {
        std::lock_guard<std::mutex> lock(llama_mutex_);
        
        // 向LLaMA进程发送查询
        fprintf(llama_process_, "%s\n", query.c_str());
        fflush(llama_process_);
        
        // 读取LLaMA的响应
        std::string response;
        char buffer[4096];
        bool response_complete = false;
        
        while (!response_complete && fgets(buffer, sizeof(buffer), llama_process_) != nullptr) {
            response += buffer;
            
            // 检查响应是否完成
            if (strstr(buffer, ">") != nullptr || response.find("\n\n>") != std::string::npos) {
                response_complete = true;
            }
        }
        
        return response;
    }

    void cleanup() {
        running_ = false;
        
        // 关闭服务器套接字
        if (server_fd_ >= 0) {
            close(server_fd_);
        }
        
        // 关闭LLaMA进程
        if (llama_process_) {
            pclose(llama_process_);
        }
    }

    std::string model_path_;  // LLaMA模型的路径
    int port_;                // 服务器监听的端口号
    int server_fd_ = -1;      // 服务器的套接字文件描述符。-1 表示初始无效。
    FILE* llama_process_ = nullptr; // 指向 LLaMA 子进程的文件指针。nullptr 表示初始没有进程。
    std::atomic<bool> running_; // 一个原子布尔值，用于控制服务器主循环是否继续运行。
    std::mutex llama_mutex_;   // 互斥锁，用来保护对 llama_process_ 的访问。
    bool use_gpu_;            // 是否使用GPU
    int gpu_layers_;          // 在GPU上运行的层数
};

// 信号处理
std::atomic<bool> g_running(true);
void signalHandler(int signal) {
    g_running = false;
}

int main(int argc, char* argv[]) {
    // 设置信号处理
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    
    try {
        // 配置模型路径和端口
        std::string model_path = "/home/shl203/llama.cpp/models/qwen/Qwen-7B-Chat.Q4_K_M.gguf";
        int port = 8899;
        bool use_gpu = false;
        int gpu_layers = 0;
        
        // 从命令行参数获取配置（如有）
        if (argc > 1) model_path = argv[1];
        if (argc > 2) port = std::stoi(argv[2]);
        if (argc > 3) use_gpu = (std::string(argv[3]) == "--gpu");
        if (argc > 4) gpu_layers = std::stoi(argv[4]);

        if (use_gpu && gpu_layers <= 0) {
            std::cerr << "错误: 使用GPU时必须指定层数" << std::endl;
            return 1;
        }

        // 创建并启动服务器
        LlamaServer server(model_path, port, use_gpu, gpu_layers);
        server.start();
        
    } catch (const std::exception& e) {
        std::cerr << "错误: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}