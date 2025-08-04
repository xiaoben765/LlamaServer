#include <iostream>
#include <string>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fstream>
#include <vector>
#include <iomanip>
#include <sstream>
#include "nlohmann/json.hpp"  // 添加到文件顶部

#define PORT 8899
#define BUFFER_SIZE 4096

using json = nlohmann::json;

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

    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        std::cerr << "❌ 绑定端口失败" << std::endl;
        return -1;
    }

    if (listen(server_fd, 3) < 0) {
        std::cerr << "❌ 监听失败" << std::endl;
        return -1;
    }

    std::cout << "🚀 LLaMA TCP 服务已启动，监听端口 " << PORT << std::endl;

    while (true) {
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen)) < 0) {
            std::cerr << "❌ 接受连接失败" << std::endl;
            continue;
        }

        // 读取客户端的提示词
        memset(buffer, 0, BUFFER_SIZE);
        ssize_t bytes_read = read(new_socket, buffer, BUFFER_SIZE - 1);
        if (bytes_read <= 0) {
            std::cerr << "❌ 读取客户端数据失败或连接关闭" << std::endl;
            close(new_socket);
            continue;
        }
        
        std::string prompt(buffer);
        std::cout << "📥 收到请求：" << prompt << std::endl;

        // 添加请求验证
        if (prompt.empty()) {
            std::cout << "⚠️ 收到空请求，返回错误信息" << std::endl;
            json error_response;
            error_response["response"] = "请求不能为空";
            error_response["cached"] = false;
            std::string error_json = error_response.dump();
            send(new_socket, error_json.c_str(), error_json.size(), 0);
            close(new_socket);
            continue;
        }

        // 清理请求中的空白字符
        std::string trimmed_prompt = prompt;
        trimmed_prompt.erase(0, trimmed_prompt.find_first_not_of(" \t\n\r"));
        trimmed_prompt.erase(trimmed_prompt.find_last_not_of(" \t\n\r") + 1);

        if (trimmed_prompt.empty()) {
            std::cout << "⚠️ 收到空白请求，返回错误信息" << std::endl;
            json error_response;
            error_response["response"] = "请求内容不能为空白";
            error_response["cached"] = false;
            std::string error_json = error_response.dump();
            send(new_socket, error_json.c_str(), error_json.size(), 0);
            close(new_socket);
            continue;
        }

        // 使用清理后的prompt
        prompt = trimmed_prompt;

        // 使用临时文件存储prompt，避免shell转义问题
        std::string temp_prompt_file = "/tmp/llama_prompt_" + std::to_string(getpid()) + ".txt";
        
        // 将prompt写入临时文件
        {
            std::ofstream prompt_file(temp_prompt_file);
            if (!prompt_file) {
                std::cerr << "❌ 无法创建临时文件: " << temp_prompt_file << std::endl;
                std::string error_msg = "无法创建临时文件";
                send(new_socket, error_msg.c_str(), error_msg.size(), 0);
                close(new_socket);
                continue;
            }
            prompt_file << prompt;
        }
        
        // 构建使用服务器模式的命令，避免每次重新加载模型
        std::string command = "/home/shl203/llama.cpp/build/bin/main";
        command += " -m /home/shl203/llama.cpp/models/qwen/Qwen-7B-Chat.Q4_K_M.gguf";
        command += " --file " + temp_prompt_file;  // 使用文件而非--prompt参数
        command += " -n 256";  // 限制输出长度到256 tokens
        command += " --temp 0.3";  // 降低随机性，提高响应速度
        command += " --ctx-size 1024";  // 减少上下文长度
        // command += " --no-color"; // 禁用颜色输出
        // command += " --log-disable"; // 禁用日志

        if (use_gpu) {
            command += " -ngl " + std::to_string(gpu_layers > 0 ? gpu_layers : 32);
        }

        // 在执行命令后的输出处理部分
        std::cout << "🔄 执行命令: " << command << std::endl;

        // 执行命令并获取结果
        FILE *fp = popen(command.c_str(), "r");
        if (!fp) {
            std::cerr << "❌ 无法执行命令: " << strerror(errno) << std::endl;
            std::string error_msg = "LLaMA 服务错误: " + std::string(strerror(errno));
            send(new_socket, error_msg.c_str(), error_msg.size(), 0);
        } else {
            // 流式响应处理
            std::string full_response;
            bool has_output = false;
            bool skip_init_logs = true;
            
            // 流式读取但累积输出，之后一次性发送
            while (fgets(buffer, BUFFER_SIZE, fp) != nullptr) {
                std::string line(buffer);
                std::cout << "调试 - 收到行: " << line;
                
                // 改进初始化日志跳过逻辑
                if (skip_init_logs) {
                    // 跳过模型加载等信息
                    if (line.find("ggml") != std::string::npos ||
                        line.find("llama_") != std::string::npos ||
                        line.find("print_info") != std::string::npos ||
                        line.find("load_") != std::string::npos ||
                        line.find("system_info") != std::string::npos ||
                        line.find("sampler") != std::string::npos ||
                        line.find("generate:") != std::string::npos) {
                        continue;
                    }
                    
                    // 检查是否开始生成内容 - 更宽松的过滤
                    if (line.length() > 1) {
                        std::string clean_line = line;
                        
                        // 清理特殊标记但保留其他内容
                        size_t pos = clean_line.find("[end of text]");
                        if (pos != std::string::npos) {
                            clean_line = clean_line.substr(0, pos);
                        }
                        
                        pos = clean_line.find("[PAD");
                        if (pos != std::string::npos) {
                            clean_line = clean_line.substr(0, pos);
                        }
                        
                        // 只有在清理后内容为空时才跳过
                        if (!clean_line.empty() && clean_line != "\n" && clean_line != "\r\n") {
                            skip_init_logs = false;
                            has_output = true;
                            full_response += clean_line;
                        }
                    }
                } else {
                    // 继续累积输出
                    if (line.length() > 0) {
                        // 在添加行之前检测并删除特殊标记
                        std::string clean_line = line;
                        size_t pos = clean_line.find("[end of text]");
                        if (pos != std::string::npos) {
                            clean_line = clean_line.substr(0, pos);
                        }

                        pos = clean_line.find("[PAD");
                        if (pos != std::string::npos) {
                            clean_line = clean_line.substr(0, pos);
                        }

                        // 如果清理后行仍然有内容，添加到响应
                        if (!clean_line.empty() && clean_line != "\n" && clean_line != "\r\n") {
                            full_response += clean_line;
                            has_output = true;
                        }
                    }
                }
            }
            
            int status = pclose(fp);
            if (status != 0) {
                std::cerr << "⚠️ 命令返回非零状态: " << status << std::endl;
            }
            
            // 清理临时文件
            if (remove(temp_prompt_file.c_str()) != 0) {
                std::cerr << "⚠️ 无法删除临时文件: " << temp_prompt_file << ": " << strerror(errno) << std::endl;
            }
            
            // 没有输出时返回错误消息
            if (!has_output) {
                std::cerr << "⚠️ 未检测到有效输出，原始响应长度: " << full_response.length() << std::endl;
                // 尝试从未过滤的输出中提取内容
                if (full_response.empty()) {
                    // 重新运行命令获取更简单的输出 - 使用临时文件
                    std::string backup_prompt_file = "/tmp/llama_backup_prompt_" + std::to_string(getpid()) + ".txt";
                    {
                        std::ofstream prompt_file(backup_prompt_file);
                        if (!prompt_file) {
                            std::cerr << "❌ 无法创建备用临时文件" << std::endl;
                            full_response = "模型没有生成回复。请检查日志以获取详细信息。";
                        } else {
                            prompt_file << "请用简短的一句话回答: " << prompt;
                        }
                    }
                    std::string simple_cmd = "/home/shl203/llama.cpp/build/bin/main -m /home/shl203/llama.cpp/models/qwen/Qwen-7B-Chat.Q4_K_M.gguf --file " + backup_prompt_file + " -n 100 -ngl 32 --temp 0.5";
                    FILE* backup_fp = popen(simple_cmd.c_str(), "r");
                    if (backup_fp) {
                        std::string backup_response;
                        while (fgets(buffer, BUFFER_SIZE, backup_fp) != nullptr) {
                            backup_response += buffer;
                        }
                        pclose(backup_fp);
                        
                        // 清理备用临时文件
                        if (remove(backup_prompt_file.c_str()) != 0) {
                            std::cerr << "⚠️ 无法删除备用临时文件: " << backup_prompt_file << ": " << strerror(errno) << std::endl;
                        }
                        
                        // 提取最后一行非空内容
                        std::istringstream iss(backup_response);
                        std::string last_line;
                        std::string line;
                        while (std::getline(iss, line)) {
                            if (!line.empty() && line.find("ggml") == std::string::npos && 
                                line.find("llama") == std::string::npos) {
                                last_line = line;
                            }
                        }
                        
                        if (!last_line.empty()) {
                            full_response = last_line;
                        } else {
                            full_response = "模型没有生成回复。请检查日志以获取详细信息。";
                        }
                    } else {
                        full_response = "模型没有生成回复。请检查日志以获取详细信息。";
                    }
                }
            }
            
            // 只发送纯文本响应，不添加HTTP头和JSON格式
            // send(new_socket, full_response.c_str(), full_response.size(), 0);

            // 清理full_response中的无效UTF-8字符
            std::cout << "🧹 检查并清理无效的UTF-8字符..." << std::endl;
            std::string cleaned_response;
            cleaned_response.reserve(full_response.length());
            
            for (size_t i = 0; i < full_response.length(); ) {
                unsigned char c = static_cast<unsigned char>(full_response[i]);
                
                if (c < 0x80) {
                    // ASCII字符，直接复制
                    cleaned_response.push_back(full_response[i]);
                    i++;
                } else if (c >= 0xC0 && c <= 0xDF) {
                    // 2字节UTF-8序列
                    if (i + 1 < full_response.length() && (static_cast<unsigned char>(full_response[i+1]) & 0xC0) == 0x80) {
                        cleaned_response.push_back(full_response[i]);
                        cleaned_response.push_back(full_response[i+1]);
                        i += 2;
                    } else {
                        // 无效序列，替换
                        cleaned_response.push_back('?');
                        i++;
                    }
                } else if (c >= 0xE0 && c <= 0xEF) {
                    // 3字节UTF-8序列
                    if (i + 2 < full_response.length() && 
                        (static_cast<unsigned char>(full_response[i+1]) & 0xC0) == 0x80 && 
                        (static_cast<unsigned char>(full_response[i+2]) & 0xC0) == 0x80) {
                        cleaned_response.push_back(full_response[i]);
                        cleaned_response.push_back(full_response[i+1]);
                        cleaned_response.push_back(full_response[i+2]);
                        i += 3;
                    } else {
                        // 无效序列，替换
                        cleaned_response.push_back('?');
                        i++;
                    }
                } else if (c >= 0xF0 && c <= 0xF7) {
                    // 4字节UTF-8序列
                    if (i + 3 < full_response.length() && 
                        (static_cast<unsigned char>(full_response[i+1]) & 0xC0) == 0x80 && 
                        (static_cast<unsigned char>(full_response[i+2]) & 0xC0) == 0x80 && 
                        (static_cast<unsigned char>(full_response[i+3]) & 0xC0) == 0x80) {
                        cleaned_response.push_back(full_response[i]);
                        cleaned_response.push_back(full_response[i+1]);
                        cleaned_response.push_back(full_response[i+2]);
                        cleaned_response.push_back(full_response[i+3]);
                        i += 4;
                    } else {
                        // 无效序列，替换
                        cleaned_response.push_back('?');
                        i++;
                    }
                } else {
                    // 无效字节，替换
                    cleaned_response.push_back('?');
                    i++;
                }
            }
            
            std::cout << "🧹 清理完成，原长度: " << full_response.length() 
                      << "，清理后长度: " << cleaned_response.length() << std::endl;
            
            // 构建JSON响应，使用经过清理的响应
            json response_json;
            response_json["response"] = cleaned_response;
            response_json["cached"] = false;

            // 发送JSON响应 - 增强版本
            std::string json_response = response_json.dump();

            // 添加调试信息
            std::cout << "📤 准备发送JSON响应，长度: " << json_response.length() << std::endl;
            std::cout << "📤 JSON内容(前100字符): " << 
                      (json_response.length() > 100 ? json_response.substr(0, 100) + "..." : json_response) << std::endl;

            // 确保完整发送
            size_t total_sent = 0;
            size_t response_length = json_response.length();

            while (total_sent < response_length) {
                ssize_t sent = send(new_socket, json_response.c_str() + total_sent, 
                                   response_length - total_sent, 0);
                if (sent < 0) {
                    std::cerr << "❌ 发送响应失败: " << strerror(errno) << std::endl;
                    break;
                }
                total_sent += sent;
                std::cout << "📤 已发送 " << total_sent << "/" << response_length << " 字节" << std::endl;
            }

            if (total_sent == response_length) {
                std::cout << "✅ JSON响应发送完成" << std::endl;
            } else {
                std::cout << "⚠️ JSON响应发送不完整" << std::endl;
            }
        }

        close(new_socket);
        std::cout << "✅ 响应已发送，等待新请求..." << std::endl;
    }

    close(server_fd);
    return 0;
}