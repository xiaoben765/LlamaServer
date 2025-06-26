#pragma once

#include <functional>

#include "noncopyable.h"
#include "Socket.h" // 封装了底层的 socket 文件描述符（fd）和相关操作（bind, listen, accept等）。
#include "Channel.h" // “通道”，这是事件分发的核心。它将一个文件描述符（fd）和它感兴趣的事件（如读、写）以及事件发生时的回调函数打包在一起。

class EventLoop;
class InetAddress; // 用于表示网络地址（IP地址和端口号），通常用于绑定和连接操作。

/*Acceptor 的核心功能就是：监听服务器端口，接收新的客户端连接，然后将这个连接交给其他模块去处理。*/

class Acceptor : noncopyable
{
public:
    // 新连接的回调函数类型，接收新连接的socket文件描述符和对端地址
    // 1. int sockfd: 新建立的连接的套接字文件描述符。
    // 2. const InetAddress&: 客户端的地址信息。
    // std::function 非常灵活，可以包装普通函数、Lambda表达式、成员函数等。
    using NewConnectionCallback = std::function<void(int sockfd, const InetAddress &)>;

    /*构造函数，输入参数为事件循环、监听地址和是否重用端口*/
    // loop: Acceptor 需要在哪个 EventLoop 中运行。通常是主 EventLoop (mainLoop)。
    // listenAddr: 服务器要在哪个IP和端口上监听。
    // reuseport: 是否开启 SO_REUSEPORT 选项（允许多个进程监听同一个端口）。
    Acceptor(EventLoop *loop, const InetAddress &listenAddr, bool reuseport);
    ~Acceptor();

    //设置新连接的回调函数
    void setNewConnectionCallback(const NewConnectionCallback &cb) { NewConnectionCallback_ = cb; }
    // 判断是否在监听
    bool listenning() const { return listenning_; }
    // 监听本地端口
    void listen();

private:
    // 这是私有成员函数，是内部使用的事件处理函数。
    // 当监听的 socket 上有新连接事件到来时（可读事件），Channel 会调用这个函数。
    void handleRead();

    // ========= 成员变量 =========
    EventLoop *loop_; // 指向 EventLoop 的指针。Acceptor 属于一个 EventLoop。
    
    Socket acceptSocket_; // 监听套接字。它被封装在 Socket 对象中，方便管理。
    
    Channel acceptChannel_; // 监听套接字的“通道”。它负责将 socket 和它上面的“可读事件”（即新连接事件）与 handleRead() 函数绑定起来，并注册到 EventLoop 中。
    
    NewConnectionCallback NewConnectionCallback_; // 用户设置的回调函数，用于处理新连接。
    
    bool listenning_; // 一个标志位，表示是否正在监听。
};