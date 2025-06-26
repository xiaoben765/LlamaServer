#pragma once

/**
 * 用户使用muduo编写服务器程序
 **/

// C++ 标准库
#include <functional> // 引入 std::function，用于封装回调函数
#include <string>     // 引入 std::string，用于处理字符串
#include <memory>     // 引入智能指针 (std::unique_ptr, std::shared_ptr)，用于自动管理内存
#include <atomic>     // 引入原子类型 (std::atomic_int)，用于多线程环境下的无锁计数
#include <unordered_map> // 引入哈希表，用于高效地存储和查找连接

// 项目内部的头文件
#include "EventLoop.h"          // 事件循环，是网络库的核心，负责监听和分发事件
#include "Acceptor.h"           // 连接接收器，专门用来监听新的客户端连接
#include "InetAddress.h"        // 封装了 IP 地址和端口号
#include "noncopyable.h"        // 一个工具类，让类不能被拷贝
#include "EventLoopThreadPool.h"// EventLoop 线程池，用于管理工作线程
#include "Callbacks.h"          // 定义了各种回调函数的类型
#include "TcpConnection.h"      // 封装了一个 TCP 连接
#include "Buffer.h"             // 缓冲区，用于处理网络数据的读写

// 对外的服务器编程使用的类
class TcpServer
{
public:
    // 它用于在每个工作线程启动时执行一些用户自定义的初始化操作。
    using ThreadInitCallback = std::function<void(EventLoop *)>; // 比如设置线程的名称、优先级等。

    enum Option  // 定义一个枚举，用于设置端口复用选项
    {
        kNoReusePort,//不允许重用本地端口
        kReusePort,//允许重用本地端口
    };

    // 构造函数。输入的参数包括服务器的事件循环、监听的 IP 地址和端口号、服务器名称和端口复用选项。
    TcpServer(EventLoop *loop,
              const InetAddress &listenAddr,
              const std::string &nameArg,
              Option option = kNoReusePort);
    ~TcpServer();

    /*
        setThreadInitCallback: 设置每个工作线程启动时的初始化回调。    
        setConnectionCallback: 设置连接建立或断开时的回调函数。比如，当一个新客户端连上来时，你可以在这里打印一条欢迎信息。
        setMessageCallback: 设置收到客户端数据时的回调函数。这是处理业务逻辑最主要的地方。比如，解析客户端发来的请求并准备响应。
        setWriteCompleteCallback: 设置数据发送完毕时的回调函数。当服务器向客户端发送大量数据时，数据可能不会一次性发完。这个回调会在所有数据都写入内核缓冲区后被触发。
    */
    void setThreadInitCallback(const ThreadInitCallback &cb) { threadInitCallback_ = cb; }
    void setConnectionCallback(const ConnectionCallback &cb) { connectionCallback_ = cb; }
    void setMessageCallback(const MessageCallback &cb) { messageCallback_ = cb; }
    void setWriteCompleteCallback(const WriteCompleteCallback &cb) { writeCompleteCallback_ = cb; }

    // 设置底层subloop(工作线程)的个数
    void setThreadNum(int numThreads);
    /*
        如果 numThreads 为 0 或 1，表示所有操作（包括监听新连接和处理已建立的连接）都在 mainLoop 中完成，即单线程模式。
        如果 numThreads 大于 1，服务器会创建一个包含 numThreads 个线程的线程池；
        新连接会被分发到这些线程中去处理，从而实现并发，这就是著名的 one loop per thread 模型。
    */

    /**
     * 如果没有监听, 就启动服务器(监听).
     * 多次调用没有副作用.
     * 线程安全.
     */
    void start(); // 开始监听端口，准备接受连接

private:
    void newConnection(int sockfd, const InetAddress &peerAddr); // 有新连接到来时的回调函数，sockfd是新连接的文件描述符，peerAddr是对端地址

    // 为什么要removeConnection调用removeConnectionInLoop？
    // 因为TcpServer的removeConnection可能在不同的线程中被调用，而removeConnectionInLoop确保在正确的事件循环中执行。
    // 比如，当一个线程正在关闭连接时，另一个线程却尝试向这个连接写入数据，这会引发竞态条件。
    // 所以需要在removeConnectionInLoop中处理连接的移除, 也就是removeConnectionInLoop函数的作用
    void removeConnection(const TcpConnectionPtr &conn); // 连接断开时的回调函数，负责从连接列表connections_中移除该连接
    void removeConnectionInLoop(const TcpConnectionPtr &conn); // 在当前事件循环中移除连接，确保线程安全

    // 储存所有连接的哈希表, 使用 std::shared_ptr 可以方便地管理连接对象的生命周期。
    // 键 (std::string): 连接的唯一名称（由 name_ + nextConnId_ 生成）。
    // 值 (TcpConnectionPtr): 一个指向 TcpConnection 对象的智能指针 (TcpConnectionPtr 通常是 std::shared_ptr<TcpConnection>)。
    using ConnectionMap = std::unordered_map<std::string, TcpConnectionPtr>;

    EventLoop *loop_; // baseloop 用户自定义的loop, 职责是
                      // 1. 启动服务器监听
                      // 2. 创建线程池
                      // 3. 分发新连接到线程池中的subloop

    const std::string ipPort_; // 服务器监听的IP地址和端口号，格式为 "IP:Port"，例如 "127.0.0.1:8080"
    const std::string name_;  // 服务器名称

    std::unique_ptr<Acceptor> acceptor_; // 运行在mainloop 任务就是监听新连接事件

    std::shared_ptr<EventLoopThreadPool> threadPool_; // one loop per thread，这个线程池管理着所有的工作线程（subLoop）

    ConnectionCallback connectionCallback_;       //有新连接时的回调
    MessageCallback messageCallback_;             // 有读写事件发生时的回调
    WriteCompleteCallback writeCompleteCallback_; // 消息发送完成后的回调

    ThreadInitCallback threadInitCallback_; // loop线程初始化的回调
    int numThreads_;//线程池中线程的数量。
    std::atomic_int started_; // 服务器是否已经启动的标志，使用原子操作以确保线程安全
    int nextConnId_; // 下一个连接的ID，用于生成唯一的连接名称，避免冲突
    ConnectionMap connections_; // 保存所有的连接
};