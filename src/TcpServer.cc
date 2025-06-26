#include <functional>
#include <string.h>

#include <TcpServer.h>
#include <Logger.h>
#include <TcpConnection.h>

// 检查传入的EventLoop指针是否为nullptr
static EventLoop *CheckLoopNotNull(EventLoop *loop)
{
    if (loop == nullptr)
    {
        LOG_FATAL<<"main Loop is NULL!"; // 如果传入的EventLoop指针为nullptr，记录一个致命错误日志
        // FATAL 级别的日志不仅会打印消息，还会直接终止整个程序。
    }
    return loop; // 如果不为空，返回传入的EventLoop指针
}

TcpServer::TcpServer(EventLoop *loop,
                       const InetAddress &listenAddr,
                       const std::string &nameArg,
                       Option option)
    : loop_(CheckLoopNotNull(loop)) // 1. 初始化 mainLoop， mainLoop的目的是
                                                                        // 1. 启动服务器监听Acceptor
                                                                        // 2. 创建线程池
                                                                        // 3. 分发新连接到线程池中的subLoop
    , ipPort_(listenAddr.toIpPort()) // 2. 初始化 IP 和端口字符串
    , name_(nameArg) // 3. 初始化服务器名称
    , acceptor_(new Acceptor(loop, listenAddr, option == kReusePort)) // 4. 创建 Acceptor，输入参数为事件循环、监听地址和端口复用选项， 目的是监听新连接事件
    , threadPool_(new EventLoopThreadPool(loop, name_)) // 5. 创建线程池，输入参数为事件循环和服务器名称，目的是管理多个工作线程（subLoop）
    , connectionCallback_() // 6. 默认初始化回调， 用于处理连接建立或断开事件
    , messageCallback_() // 7. 默认初始化回调， 用于处理消息接收事件
    , nextConnId_(1) // 8. 初始化连接 ID
    , started_(0) // 9. 初始化启动状态， 0表示未启动，1表示已启动
{
    // 当有新用户连接时，Acceptor类中绑定的acceptChannel_会有读事件发生，执行handleRead()调用TcpServer::newConnection回调
    acceptor_->setNewConnectionCallback(
        std::bind(&TcpServer::newConnection, this, std::placeholders::_1, std::placeholders::_2));
        // 成员函数不能独立存在，必须由一个对象来调用。this 就是指向当前正在构造的 TcpServer 对象的指针。
        // 输入参数为占位符
        // std::placeholders::_1: 新连接的文件描述符
        // std::placeholders::_2: 新连接的对端地址
}

TcpServer::~TcpServer()
{
    for(auto &item : connections_) // 遍历 connections_ 哈希表，item.first为连接名称，item.second为TcpConnectionPtr智能指针
    {
        TcpConnectionPtr conn(item.second);  // 创建一个新的智能指针，指向当前连接的TcpConnection对象
        item.second.reset();    // 把原始的智能指针复位 让栈空间的TcpConnectionPtr conn指向该对象 当conn出了其作用域 即可释放智能指针指向的对象
        // 为什么要拷贝？因为TcpConnection对象可能会在不同的线程中被访问。
        // 拷贝的目的是希望派发一个连接销毁的事件到对应的EventLoop中去处理，而不是直接在当前线程中销毁连接对象。

        // 销毁连接
        // conn->getLoop()是指向当前连接TcpConnection所属的EventLoop对象，此行代码就是获取管理它的那个 EventLoop。
        conn->getLoop()->runInLoop(
            std::bind(&TcpConnection::connectDestroyed, conn)); // 在对应的EventLoop中执行连接销毁操作，避免了线程安全问题
    }
}

// 设置底层subloop的个数
void TcpServer::setThreadNum(int numThreads)
{
    // 为什么要再赋值一次？因为numThreads_是TcpServer的成员变量，numThreads是传入的参数。
    int numThreads_=numThreads;  // 这么做是为了在后续代码中使用成员变量。

    threadPool_->setThreadNum(numThreads_); // 设置线程池的线程数, EventLoopThreadPool表示事件循环线程池，负责管理多个 EventLoop 对象
}

// 开启服务器监听
void TcpServer::start()
{
    // fetch_add(1) 是一个原子操作，确保在多线程环境下对 started_ 的修改是安全的。
    if (started_.fetch_add(1) == 0)    // 防止一个TcpServer对象被start多次
    {
        threadPool_->start(threadInitCallback_);    // 启动底层的loop线程池
        loop_->runInLoop(std::bind(&Acceptor::listen, acceptor_.get())); // 在主事件循环中调用 Acceptor 的 listen 方法开始监听新连接
        // acceptor_.get() 从 unique_ptr 中获取原始指针
    }
}

// 有一个新用户连接，acceptor会执行这个回调操作，负责将mainLoop接收到的请求连接(acceptChannel_会有读事件发生)通过回调轮询分发给subLoop去处理
void TcpServer::newConnection(int sockfd, const InetAddress &peerAddr)
{
    // 轮询算法 选择一个subLoop 来管理connfd对应的channel
    // getNextLoop 函数会根据传入的IP地址来选择一个EventLoop对象，这样可以确保同一IP的连接总是分配到同一个EventLoop上。
    EventLoop *ioLoop = threadPool_->getNextLoop(peerAddr.toIp());

    char buf[64] = {0}; // 定义一个缓冲区，用于存储连接名称
    // 生成连接名称，格式为 "-ip:port#id"，其中 ipPort_ 是服务器的 IP 地址和端口，nextConnId_ 是下一个连接的 ID
    // snprintf 函数用于格式化字符串，将生成的连接名称存储到 buf 中，确保不会超过 buf 的大小
    snprintf(buf, sizeof buf, "-%s#%d", ipPort_.c_str(), nextConnId_);
    ++nextConnId_;  // 这里没有设置为原子类是因为其只在mainloop中执行 不涉及线程安全问题
    std::string connName = name_ + buf;

    // 输出内容例如： TcpServer::newConnection [test]- new connection [-192.168.1.1:8080#1]from 192.168.1.2:12345
    // c_str()函数返回一个指向字符串内容的指针，便于输出到日志中
    LOG_INFO<<"TcpServer::newConnection ["<<name_.c_str()<<"]- new connection ["<<connName.c_str()<<"]from %s"<<peerAddr.toIpPort().c_str();
    
    // 通过sockfd获取其绑定的本机的ip地址和端口信息
    // sockaddr_in 是一个结构体，表示IPv4地址和端口信息
    sockaddr_in local;
    ::memset(&local, 0, sizeof(local)); // 清空结构体local的内容

    socklen_t addrlen = sizeof(local); // addrlen是一个sockaddr_in结构体的大小，用于存储本地地址信息的长度
    if(::getsockname(sockfd, (sockaddr *)&local, &addrlen) < 0) // 获取本地地址信息
    {
        // getsockname 函数用于获取与套接字 sockfd 关联的本地地址信息, 将获取到的本地地址信息存储到 local 中
        LOG_ERROR<<"sockets::getLocalAddr";
    }

    InetAddress localAddr(local); // 将本地地址信息转换为 InetAddress 对象，方便后续使用
    // 创建一个新的 TcpConnection 对象，传入的参数包括事件循环、连接名称、套接字文件描述符、本地地址和对端地址
    TcpConnectionPtr conn(new TcpConnection(ioLoop,
                                            connName,
                                            sockfd,
                                            localAddr,
                                            peerAddr));
    connections_[connName] = conn; // 将新创建的连接对象存储到 connections_ 哈希表中，键为连接名称，值为 TcpConnectionPtr 智能指针

    // 下面的回调都是用户设置给TcpServer => TcpConnection的，至于Channel绑定的则是TcpConnection设置的四个，handleRead,handleWrite... 这下面的回调用于handlexxx函数中
    
    /* 下面三个回调函数分别是设置连接回调、消息回调、写完成回调 */
    conn->setConnectionCallback(connectionCallback_);
    conn->setMessageCallback(messageCallback_);
    conn->setWriteCompleteCallback(writeCompleteCallback_);

    // 设置了如何关闭连接的回调
    conn->setCloseCallback(
        std::bind(&TcpServer::removeConnection, this, std::placeholders::_1));

    // 把 TcpConnection::connectEstablished 这个任务派发给选定的 subLoop 去执行，此处subLoop是 ioLoop。
    ioLoop->runInLoop(
        std::bind(&TcpConnection::connectEstablished, conn));
}

void TcpServer::removeConnection(const TcpConnectionPtr &conn)
{
    loop_->runInLoop(
        std::bind(&TcpServer::removeConnectionInLoop, this, conn));
}

void TcpServer::removeConnectionInLoop(const TcpConnectionPtr &conn)
{
    LOG_INFO<<"TcpServer::removeConnectionInLoop ["<<
             name_.c_str()<<"] - connection %s"<<conn->name().c_str();

    connections_.erase(conn->name());  // 从 connections_ 哈希表中移除该连接，键为连接名称
    EventLoop *ioLoop = conn->getLoop(); // 获取该连接所属的 EventLoop 对象
    
    ioLoop->queueInLoop(
        std::bind(&TcpConnection::connectDestroyed, conn)); // 将连接销毁的任务派发给该连接所属的 EventLoop 对象
        // 为什么要用queueInLoop而不是runInLoop？
        // 因为在连接销毁时，可能会有其他线程正在访问该连接的资源，使用 queueInLoop 可以确保在正确的线程中执行连接销毁操作，避免竞态条件。

}