#pragma once

#include "noncopyable.h"

class InetAddress;

// 封装socket fd
class Socket : noncopyable
{
public:
    explicit Socket(int sockfd) // explicit 关键字防止隐式转换，也就是构造对象时必须为Socket类提供一个int类型的sockfd参数，如Socket s(5);
        : sockfd_(sockfd)
    {
    }
    ~Socket();

    int fd() const { return sockfd_; } // 获取socket的文件描述符
    void bindAddress(const InetAddress &localaddr); // 绑定地址到socket
    void listen();
    int accept(InetAddress *peeraddr); // 接受连接，并返回新的socket文件描述符，同时填充peeraddr为对端地址

    void shutdownWrite(); // 关闭写端

    void setTcpNoDelay(bool on); // 设置TCP_NODELAY选项，禁用Nagle算法
    void setReuseAddr(bool on);  // 设置SO_REUSEADDR选项，允许地址重用
    void setReusePort(bool on); // 设置SO_REUSEPORT选项，允许端口重用
    void setKeepAlive(bool on); // 设置SO_KEEPALIVE选项，启用TCP KeepAlive

private:
    const int sockfd_;
};
