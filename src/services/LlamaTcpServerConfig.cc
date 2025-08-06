#include "services/LlamaTcpServer.h"
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>

using namespace llama;

LlamaTcpServer::LlamaTcpServer(int port) : server_fd_(-1), is_running_(false) {
    config_.port = port;
}

LlamaTcpServer::~LlamaTcpServer() {
    if (server_fd_ >= 0) {
        close(server_fd_);
    }
}

bool LlamaTcpServer::initialize(int argc, char* argv[]) {
    std::cout << "🚀 初始化 LLaMA TCP 服务器..." << std::endl;
    
    if (!parseArguments(argc, argv)) {
        return false;
    }
    
    if (!setupSocket()) {
        return false;
    }
    
    std::cout << "✅ LLaMA TCP 服务器初始化完成" << std::endl;
    return true;
}

bool LlamaTcpServer::parseArguments(int argc, char* argv[]) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--gpu") {
            config_.use_gpu = true;
        } else if (arg.rfind("--gpu-layers=", 0) == 0) {
            config_.gpu_layers = std::stoi(arg.substr(13));
        }
    }

    if (config_.use_gpu) {
        std::cout << "🚀 启用 GPU 模式，层数: " << config_.gpu_layers << std::endl;
    } else {
        std::cout << "🚀 使用 CPU 模式" << std::endl;
    }
    
    return true;
}

void LlamaTcpServer::checkGpuStatus(const std::string& label) {
    std::string gpu_check = "nvidia-smi --query-gpu=utilization.gpu,memory.used --format=csv,noheader";
    FILE *gpu_fp = popen(gpu_check.c_str(), "r");
    if (gpu_fp) {
        char gpu_info[256];
        if (fgets(gpu_info, sizeof(gpu_info), gpu_fp)) {
            std::cout << "📊 GPU状态(" << label << "): " << gpu_info;
        }
        pclose(gpu_fp);
    }
}
