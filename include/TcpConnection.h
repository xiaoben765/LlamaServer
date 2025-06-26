#pragma once

#include <memory>      // 引入智能指针, 如 std::unique_ptr, std::shared_ptr
#include <string>      // 引入 std::string 类
#include <atomic>      // 引入原子类型, 如 std::atomic_int, 用于多线程安全的状态管理

#include "noncopyable.h"   // 一个工具类，禁止对象被拷贝
#include "InetAddress.h"   // 封装了IP地址和端口号的类
#include "Callbacks.h"     // 定义了各种回调函数的类型
#include "Buffer.h"        // 一个缓冲区类，用于高效地处理数据的接收和发送
#include "Timestamp.h"     // 一个时间戳类

// 前向声明类
class Channel;
class EventLoop;
class Socket;

/**
 * TcpServer => Acceptor => 有一个新用户连接，通过accept函数拿到connfd
 * => TcpConnection设置回调 => 设置到Channel => Poller => Channel回调
 **/

class TcpConnection : noncopyable, public std::enable_shared_from_this<TcpConnection>
// public std::enable_shared_from_this<TcpConnection> 用于解决在类的成员函数内部获取指向自身的 std::shared_ptr 的问题
// 使得 TcpConnection 可以安全地在异步回调中使用 shared_ptr 来管理自身的生命周期
{
public:
    // 初始化一个连接：它所属的 EventLoop、一个名字、操作系统分配的 sockfd、本地地址和对端地址。
    TcpConnection(EventLoop *loop,
                  const std::string &nameArg,
                  int sockfd,
                  const InetAddress &localAddr,
                  const InetAddress &peerAddr);
    ~TcpConnection();

    EventLoop *getLoop() const { return loop_; } // 获取当前连接所属的 EventLoop
    const std::string &name() const { return name_; } // 获取连接的名称
    const InetAddress &localAddress() const { return localAddr_; } // 获取本地地址
    const InetAddress &peerAddress() const { return peerAddr_; } // 获取对端地址

    bool connected() const { return state_ == kConnected; } // 判断连接是否处于已连接状态

    // 发送数据
    void send(const std::string &buf); // 发送字符串数据
    void sendFile(int fileDescriptor, off_t offset, size_t count); // 发送文件数据
    
    // 关闭半连接: 只关闭写端
    void shutdown();

    /*
        * 设置回调函数
        * 这些回调函数会在特定事件发生时被调用，如连接建立、消息到达、写完成等
        * 用户可以通过 TcpServer 注册这些回调函数
        * TcpServer 再将注册的回调传递给 TcpConnection，TcpConnection 再将回调注册到 Channel 中
    */
    void setConnectionCallback(const ConnectionCallback &cb)
    { connectionCallback_ = cb; }
    void setMessageCallback(const MessageCallback &cb)
    { messageCallback_ = cb; }
    void setWriteCompleteCallback(const WriteCompleteCallback &cb)
    { writeCompleteCallback_ = cb; }
    void setCloseCallback(const CloseCallback &cb)
    { closeCallback_ = cb; }
    void setHighWaterMarkCallback(const HighWaterMarkCallback &cb, size_t highWaterMark)
    { highWaterMarkCallback_ = cb; highWaterMark_ = highWaterMark; }

    // 连接建立
    void connectEstablished();
    // 连接销毁
    void connectDestroyed();

private:
    enum StateE // 状态机，表示连接的状态
    {
        kDisconnected, // 已经断开连接
        kConnecting,   // 正在连接
        kConnected,    // 已连接
        kDisconnecting // 正在断开连接
    };
    void setState(StateE state) { state_ = state; }

    void handleRead(Timestamp receiveTime);
    void handleWrite();//处理写事件
    void handleClose();
    void handleError();

    void sendInLoop(const void *data, size_t len);
    void shutdownInLoop();
    void sendFileInLoop(int fileDescriptor, off_t offset, size_t count);

    EventLoop *loop_; // 这里是baseloop还是subloop由TcpServer中创建的线程数决定 若为多Reactor 该loop_指向subloop 若为单Reactor 该loop_指向baseloop
    const std::string name_;
    std::atomic_int state_;
    bool reading_;//连接是否在监听读事件

    // Socket Channel 这里和Acceptor类似    Acceptor => mainloop    TcpConnection => subloop
    std::unique_ptr<Socket> socket_;
    std::unique_ptr<Channel> channel_; // Channel 是 socket 和事件处理逻辑之间的桥梁。它将 socket 注册到 Poller，并设置好 handleRead,
                                       // handleWrite, handleClose 等事件回调函数。当 Poller 检测到 socket 上有事件发生时，
                                       // 它会调用 Channel 的 handleEvent 方法，从而触发相应的事件处理逻辑。

    const InetAddress localAddr_;
    const InetAddress peerAddr_;

    // 这些回调TcpServer也有 用户通过写入TcpServer注册 TcpServer再将注册的回调传递给TcpConnection TcpConnection再将回调注册到Channel中
    ConnectionCallback connectionCallback_;       // 有新连接时的回调
    MessageCallback messageCallback_;             // 有读写消息时的回调
    WriteCompleteCallback writeCompleteCallback_; // 消息发送完成以后的回调
    HighWaterMarkCallback highWaterMarkCallback_; // 高水位回调，什么是高水位？当发送缓冲区的大小超过设定的高水位阈值时触发，进行流量控制
    CloseCallback closeCallback_; // 关闭连接的回调
    size_t highWaterMark_; // 高水位阈值

    // 数据缓冲区
    Buffer inputBuffer_;    // 接收数据的缓冲区
    Buffer outputBuffer_;   // 发送数据的缓冲区 用户send向outputBuffer_发
};
