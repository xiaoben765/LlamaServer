#include "services/LlamaTcpServer.h"
#include <fstream>
#include <iostream>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <cerrno>

using namespace llama;

std::string LlamaTcpServer::createTempFile(const std::string& content) {
    std::string temp_prompt_file = "/tmp/llama_prompt_" + std::to_string(getpid()) + ".txt";
    
    std::ofstream prompt_file(temp_prompt_file);
    if (!prompt_file) {
        std::cerr << "❌ 无法创建临时文件: " << temp_prompt_file << std::endl;
        return "";
    }
    
    prompt_file << content;
    prompt_file.close();
    
    return temp_prompt_file;
}

void LlamaTcpServer::removeTempFile(const std::string& filepath) {
    if (remove(filepath.c_str()) != 0) {
        std::cerr << "⚠️ 无法删除临时文件: " << filepath << ": " << strerror(errno) << std::endl;
    }
}

std::string LlamaTcpServer::cleanUtf8String(const std::string& input) {
    std::cout << "🧹 检查并清理无效的UTF-8字符..." << std::endl;
    std::string cleaned_response;
    cleaned_response.reserve(input.length());
    
    for (size_t i = 0; i < input.length(); ) {
        unsigned char c = static_cast<unsigned char>(input[i]);
        
        if (c < 0x80) {
            // ASCII字符，直接复制
            cleaned_response.push_back(input[i]);
            i++;
        } else if (c >= 0xC0 && c <= 0xDF) {
            // 2字节UTF-8序列
            if (i + 1 < input.length() && (static_cast<unsigned char>(input[i+1]) & 0xC0) == 0x80) {
                cleaned_response.push_back(input[i]);
                cleaned_response.push_back(input[i+1]);
                i += 2;
            } else {
                cleaned_response.push_back('?');
                i++;
            }
        } else if (c >= 0xE0 && c <= 0xEF) {
            // 3字节UTF-8序列
            if (i + 2 < input.length() && 
                (static_cast<unsigned char>(input[i+1]) & 0xC0) == 0x80 && 
                (static_cast<unsigned char>(input[i+2]) & 0xC0) == 0x80) {
                cleaned_response.push_back(input[i]);
                cleaned_response.push_back(input[i+1]);
                cleaned_response.push_back(input[i+2]);
                i += 3;
            } else {
                cleaned_response.push_back('?');
                i++;
            }
        } else if (c >= 0xF0 && c <= 0xF7) {
            // 4字节UTF-8序列
            if (i + 3 < input.length() && 
                (static_cast<unsigned char>(input[i+1]) & 0xC0) == 0x80 && 
                (static_cast<unsigned char>(input[i+2]) & 0xC0) == 0x80 && 
                (static_cast<unsigned char>(input[i+3]) & 0xC0) == 0x80) {
                cleaned_response.push_back(input[i]);
                cleaned_response.push_back(input[i+1]);
                cleaned_response.push_back(input[i+2]);
                cleaned_response.push_back(input[i+3]);
                i += 4;
            } else {
                cleaned_response.push_back('?');
                i++;
            }
        } else {
            // 无效字节，替换
            cleaned_response.push_back('?');
            i++;
        }
    }
    
    std::cout << "🧹 清理完成，原长度: " << input.length() 
              << "，清理后长度: " << cleaned_response.length() << std::endl;
    
    return cleaned_response;
}
