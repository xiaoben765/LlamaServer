#include <iostream>
#include <string>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fstream>
#include <vector>

#define PORT 8899
#define BUFFER_SIZE 4096

void check_gpu_status(const std::string& label) {
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

int main(int argc, char *argv[]) {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    char buffer[BUFFER_SIZE] = {0};
    
    // 解析命令行参数
    bool use_gpu = false;
    int gpu_layers = 0;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--gpu") {
            use_gpu = true;
        } else if (arg.rfind("--gpu-layers=", 0) == 0) {
            gpu_layers = std::stoi(arg.substr(13));
        }
    }

    // 输出启动模式
    if (use_gpu) {
        std::cout << "🚀 启用 GPU 模式，层数: " << gpu_layers << std::endl;
    } else {
        std::cout << "🚀 使用 CPU 模式" << std::endl;
    }

    // 创建套接字
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        std::cerr << "❌ Socket 创建失败" << std::endl;
        return -1;
    }

    // 设置端口复用
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    // 绑定端口
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        std::cerr << "❌ 绑定端口失败" << std::endl;
        return -1;
    }

    // 监听端口
    if (listen(server_fd, 3) < 0) {
        std::cerr << "❌ 监听失败" << std::endl;
        return -1;
    }

    std::cout << "🚀 LLaMA TCP 服务已启动，监听端口 " << PORT << std::endl;

    while (true) {
        // 接受客户端连接
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen)) < 0) {
            std::cerr << "❌ 接受连接失败" << std::endl;
            continue;
        }

        // 读取客户端的提示词
        memset(buffer, 0, BUFFER_SIZE);
        read(new_socket, buffer, BUFFER_SIZE);
        std::string prompt(buffer);
        std::cout << "📥 收到请求：" << prompt << std::endl;

        // 构建 LLaMA 启动命令
        std::string command = "/home/shl203/llama.cpp/build/bin/main -m /home/shl203/llama.cpp/models/qwen/Qwen-7B-Chat.Q4_K_M.gguf --interactive --color -p \"" + prompt + "\" -n 512";
        
        // 根据是否使用 GPU 添加 GPU 参数
        if (use_gpu) {
            // 使用指定层数在GPU上，而不是硬编码32层
            command += " -ngl " + std::to_string(gpu_layers > 0 ? gpu_layers : 32);
            // 添加更多GPU优化参数
            command += " -b 8 --threads 4 --mlock";
        }

        // 输出完整命令以便调试
        std::cout << "🔍 执行命令: " << command << std::endl;

        // 在启动LLaMA之前执行检查
        if (use_gpu) {
            std::cout << "🔍 检查 GPU 状态..." << std::endl;
            std::string gpu_check = "nvidia-smi --query-gpu=utilization.gpu,memory.used,name --format=csv,noheader";
            FILE *gpu_fp = popen(gpu_check.c_str(), "r");
            if (gpu_fp) {
                char gpu_info[256];
                if (fgets(gpu_info, sizeof(gpu_info), gpu_fp)) {
                    std::cout << "📊 GPU状态: " << gpu_info;
                } else {
                    std::cout << "⚠️ 无法读取 GPU 状态" << std::endl;
                }
                pclose(gpu_fp);
            } else {
                std::cout << "⚠️ 无法执行 nvidia-smi 命令，请确认 NVIDIA 驱动已正确安装" << std::endl;
            }
            
            // 添加更详细的 CUDA 环境信息
            std::cout << "🔍 CUDA 环境检测..." << std::endl;
            system("ls -l /usr/local/cuda/lib64/libcudart* 2>/dev/null || echo '⚠️ 未找到 CUDA 运行时库'");
        }

        // 执行前检查 GPU
        check_gpu_status("执行前");

        // 启动 LLaMA 进程
        FILE *fp = popen(command.c_str(), "r");
        if (!fp) {
            std::cerr << "❌ 无法启动 LLaMA 进程" << std::endl;
            std::string error_msg = "LLaMA 服务启动失败，请检查配置。\n";
            send(new_socket, error_msg.c_str(), error_msg.size(), 0);
            close(new_socket);
            continue;
        }

        // 实时读取 LLaMA 响应并发送给客户端
        while (fgets(buffer, BUFFER_SIZE, fp) != nullptr) {
            std::string chunk(buffer);
            send(new_socket, chunk.c_str(), chunk.size(), 0);
            std::cout << "📤 响应：" << chunk;  // 实时打印流式响应
        }

        // 执行后检查 GPU
        check_gpu_status("执行后");

        pclose(fp);
        close(new_socket);
        std::cout << "✅ 响应已发送，等待新请求..." << std::endl;
    }

    close(server_fd);
    return 0;
}
