#pragma once

#include <string>
#include <memory>

namespace llama {

class LlamaTcpServer {
public:
    LlamaTcpServer(int port = 8899);
    ~LlamaTcpServer();
    
    bool initialize(int argc, char* argv[]);
    int run();
    
private:
    struct Config {
        bool use_gpu = false;
        int gpu_layers = 0;
        int port = 8899;
        std::string model_path = "/home/shl203/llama.cpp/models/qwen/Qwen-7B-Chat.Q4_K_M.gguf";
        std::string llama_executable = "/home/shl203/llama.cpp/build/bin/main";
    };
    
    // 配置解析
    bool parseArguments(int argc, char* argv[]);
    
    // 网络相关
    bool setupSocket();
    void acceptConnections();
    bool handleClientConnection(int client_socket);
    
    // LLaMA处理相关
    std::string processQuery(const std::string& query);
    std::string executeLlamaCommand(const std::string& prompt_file);
    std::string cleanUtf8String(const std::string& input);
    
    // 工具函数
    void checkGpuStatus(const std::string& label);
    std::string createTempFile(const std::string& content);
    void removeTempFile(const std::string& filepath);
    
private:
    Config config_;
    int server_fd_;
    bool is_running_;
};

} // namespace llama
