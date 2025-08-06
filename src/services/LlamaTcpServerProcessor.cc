#include "services/LlamaTcpServer.h"
#include "nlohmann/json.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <cerrno>

#define BUFFER_SIZE 4096

using namespace llama;
using json = nlohmann::json;

std::string LlamaTcpServer::processQuery(const std::string& query) {
    // 添加请求验证
    if (query.empty()) {
        std::cout << "⚠️ 收到空请求，返回错误信息" << std::endl;
        json error_response;
        error_response["response"] = "请求不能为空";
        error_response["cached"] = false;
        return error_response.dump();
    }

    // 清理请求中的空白字符
    std::string trimmed_prompt = query;
    trimmed_prompt.erase(0, trimmed_prompt.find_first_not_of(" \t\n\r"));
    trimmed_prompt.erase(trimmed_prompt.find_last_not_of(" \t\n\r") + 1);

    if (trimmed_prompt.empty()) {
        std::cout << "⚠️ 收到空白请求，返回错误信息" << std::endl;
        json error_response;
        error_response["response"] = "请求内容不能为空白";
        error_response["cached"] = false;
        return error_response.dump();
    }

    // 使用临时文件存储prompt
    std::string temp_prompt_file = createTempFile(trimmed_prompt);
    if (temp_prompt_file.empty()) {
        json error_response;
        error_response["response"] = "无法创建临时文件";
        error_response["cached"] = false;
        return error_response.dump();
    }

    // 执行LLaMA命令
    std::string response = executeLlamaCommand(temp_prompt_file);
    
    // 清理临时文件
    removeTempFile(temp_prompt_file);

    return response;
}

std::string LlamaTcpServer::executeLlamaCommand(const std::string& prompt_file) {
    // 构建命令
    std::string command = config_.llama_executable;
    command += " -m " + config_.model_path;
    command += " --file " + prompt_file;
    command += " -n 256";
    command += " --temp 0.3";
    command += " --ctx-size 1024";

    if (config_.use_gpu) {
        command += " -ngl " + std::to_string(config_.gpu_layers > 0 ? config_.gpu_layers : 32);
    }

    std::cout << "🔄 执行命令: " << command << std::endl;

    // 执行命令并获取结果
    FILE *fp = popen(command.c_str(), "r");
    if (!fp) {
        std::cerr << "❌ 无法执行命令: " << strerror(errno) << std::endl;
        json error_response;
        error_response["response"] = "LLaMA 服务错误: " + std::string(strerror(errno));
        error_response["cached"] = false;
        return error_response.dump();
    }

    // 处理输出
    std::string full_response;
    bool has_output = false;
    bool skip_init_logs = true;
    char buffer[BUFFER_SIZE];
    
    while (fgets(buffer, BUFFER_SIZE, fp) != nullptr) {
        std::string line(buffer);
        std::cout << "调试 - 收到行: " << line;
        
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
            
            if (line.length() > 1) {
                std::string clean_line = line;
                
                // 清理特殊标记
                size_t pos = clean_line.find("[end of text]");
                if (pos != std::string::npos) {
                    clean_line = clean_line.substr(0, pos);
                }
                
                pos = clean_line.find("[PAD");
                if (pos != std::string::npos) {
                    clean_line = clean_line.substr(0, pos);
                }
                
                if (!clean_line.empty() && clean_line != "\n" && clean_line != "\r\n") {
                    skip_init_logs = false;
                    has_output = true;
                    full_response += clean_line;
                }
            }
        } else {
            if (line.length() > 0) {
                std::string clean_line = line;
                size_t pos = clean_line.find("[end of text]");
                if (pos != std::string::npos) {
                    clean_line = clean_line.substr(0, pos);
                }

                pos = clean_line.find("[PAD");
                if (pos != std::string::npos) {
                    clean_line = clean_line.substr(0, pos);
                }

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
    
    // 处理没有输出的情况
    if (!has_output || full_response.empty()) {
        std::cerr << "⚠️ 未检测到有效输出" << std::endl;
        full_response = "模型没有生成回复。请检查日志以获取详细信息。";
    }
    
    // 清理UTF-8字符
    std::string cleaned_response = cleanUtf8String(full_response);
    
    // 构建JSON响应
    json response_json;
    response_json["response"] = cleaned_response;
    response_json["cached"] = false;

    std::string json_response = response_json.dump();
    std::cout << "📤 准备发送JSON响应，长度: " << json_response.length() << std::endl;
    
    return json_response;
}
