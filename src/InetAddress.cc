#include <strings.h>
#include <string.h>

#include <InetAddress.h>

InetAddress::InetAddress(uint16_t port, std::string ip) // 构造函数
{
    ::memset(&addr_, 0, sizeof(addr_)); // addr_是私有成员变量，初始化为0
    // addr_的成员变量含义：sin_family表示地址族，sin_port表示端口号，sin_addr表示IP地址。
    addr_.sin_family = AF_INET;  // AF_INET表示IPv4地址族
    addr_.sin_port = ::htons(port); // 本地字节序转为网络字节序. htons 代表 "host to network short" (主机转网络短整型)。
    addr_.sin_addr.s_addr = ::inet_addr(ip.c_str()); // 将字符串形式的IP地址转换为网络字节序的整数形式
}

// 二进制的IP地址转换回字符串
std::string InetAddress::toIp() const
{
    // addr_
    char buf[64] = {0}; // 存储IP地址的字符串形式 <IPv6地址最长也不超过45个字符>
    ::inet_ntop(AF_INET, &addr_.sin_addr, buf, sizeof buf); // inet_ntop 表示 inet_network to presentation
    /*
        * inet_ntop函数的作用是将二进制的IP地址转换为可读的字符串形式。
        * AF_INET表示IPv4地址族，&addr_.sin_addr是指向IPv4地址的指针，buf是存储结果的缓冲区，sizeof buf是缓冲区大小。
        * 返回值是一个指向缓冲区的指针，如果转换失败则返回NULL。    
    */
    return buf;
}

// 在 toIp() 的基础上增加了端口号
std::string InetAddress::toIpPort() const
{
    // ip:port
    char buf[64] = {0};
    ::inet_ntop(AF_INET, &addr_.sin_addr, buf, sizeof buf);

    size_t end = ::strlen(buf);
    uint16_t port = ::ntohs(addr_.sin_port); // ntohs 代表 "network to host short" (网络转主机短整型)
    sprintf(buf+end, ":%u", port); // buf + end: 指针运算，指向 buf 中IP地址字符串末尾的 \0 字符。
    return buf;
}

// 返回端口号，网络字节序转换为主机字节序
uint16_t InetAddress::toPort() const
{
    return ::ntohs(addr_.sin_port);
}

#if 0
#include <iostream>
int main()
{
    InetAddress addr(8080);
    std::cout << addr.toIpPort() << std::endl;
}
#endif