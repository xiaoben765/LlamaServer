#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>
#include <string>

// 封装socket地址类型
class InetAddress
{
public:
    explicit InetAddress(uint16_t port = 0, std::string ip = "127.0.0.1");
    // 函数重载，允许使用sockaddr_in结构体直接初始化InetAddress对象
    explicit InetAddress(const sockaddr_in &addr) // sockaddr_in 是一个结构体，表示一个IPv4地址和端口
        : addr_(addr)
    {
    }

    std::string toIp() const; // 返回点分十进制的IP地址字符串（如 "127.0.0.1"）。
    std::string toIpPort() const; // 返回 "IP:端口" 格式的字符串（如 "127.0.0.1:8080"）。
    uint16_t toPort() const; // 返回端口号，转换为主机字节序。如果端口号为 8080，则返回 8080。

    const sockaddr_in *getSockAddr() const { return &addr_; } // 返回指向sockaddr_in结构体的指针，表示该InetAddress对象的地址信息。
    void setSockAddr(const sockaddr_in &addr) { addr_ = addr; } // 设置sockaddr_in结构体，允许修改InetAddress对象的地址信息。

private:
    sockaddr_in addr_;
};