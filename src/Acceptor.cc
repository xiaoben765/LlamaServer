#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <unistd.h>
#include <Acceptor.h>
#include <Logger.h>
#include <InetAddress.h>

// 这是一个静态辅助函数，意味着它的作用域仅限于这个 .cpp 文件。
// 它的功能是创建一个非阻塞的、用于监听的 socket。
static int createNonblocking()
{
    // ::socket(...) 调用全局命名空间下的 socket 函数。
    // AF_INET: 使用 IPv4 协议。
    // SOCK_STREAM: 使用 TCP 协议，提供面向连接的、可靠的数据流。
    // | SOCK_NONBLOCK: 这是关键！将创建的 socket 设置为非阻塞模式。
    //      - 在非阻塞模式下，accept() 函数会立即返回，即使没有新连接。这对于事件驱动模型至关重要，程序不会卡在 accept() 上。
    // | SOCK_CLOEXEC: 设置“执行时关闭”（Close-on-exec）。
    //      - 当程序通过 exec() 系列函数启动一个新程序时，这个 socket 文件描述符会自动关闭，不会被子进程继承。这是一个安全和资源管理的良好实践。
    // IPPROTO_TCP: 指定使用 TCP 协议。

    // IPv4 协议属于网络层协议，负责在网络中为设备分配地址和实现数据包的路由转发；
    // TCP 协议属于传输层协议，负责在应用程序之间建立连接和实现可靠的数据传输。
    // TCP 协议的数据需要通过 IPv4 协议进行传输，IPv4 协议为 TCP 协议提供了网络传输的基础。
    int sockfd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, IPPROTO_TCP);
    if (sockfd < 0)
    {
        // 如果创建失败，记录一条致命错误日志并终止程序。
        // LOG_FATAL 是一个宏，通常来自一个日志库（如 glog 或项目自建的 Logger）。
        LOG_FATAL << "listen socket create err " << errno;
    }
    return sockfd;
}

// 输入的参数是一个 EventLoop 指针、一个 InetAddress 对象（表示监听的地址和端口）和一个布尔值（表示是否重用端口）。
Acceptor::Acceptor(EventLoop *loop, const InetAddress &listenAddr, bool reuseport)
    : loop_(loop) // 1. 保存 EventLoop 指针
    , acceptSocket_(createNonblocking()) // 2. 创建监听 socket
    , acceptChannel_(loop, acceptSocket_.fd()) // 3. 创建 Channel
    , listenning_(false) // 4. 初始化监听状态为 false
{
    
    acceptSocket_.setReuseAddr(true); // 设置 SO_REUSEADDR 选项，表示允许服务器在重启时快速绑定到相同的 IP 和端口。
    acceptSocket_.setReusePort(reuseport); // 根据传入参数设置 SO_REUSEPORT。允许多个进程或线程监听同一 IP 和端口。
    acceptSocket_.bindAddress(listenAddr); // 5. 将 socket 绑定到指定的 IP 和端口，使其成为 “监听套接字”（Listening Socket）。

    // 6. 关键一步：设置 Channel 的读事件回调函数
    // 当 acceptChannel_ 绑定的 listenfd 上发生可读事件（即有新客户端连接）时，
    // EventLoop 会调用这里设置的回调函数。
    // std::bind 将成员函数 Acceptor::handleRead 和 this 指针（当前 Acceptor 对象）绑定在一起。
    // 这意味着，当事件发生时，实际执行的是 this->handleRead()。
    acceptChannel_.setReadCallback(
        std::bind(&Acceptor::handleRead, this));
}

Acceptor::~Acceptor()
{
    // 通知 EventLoop (内部通过 Poller) 不再关心这个 Channel 上的任何事件
    acceptChannel_.disableAll();    // 把从Poller中感兴趣的事件删除掉

    // 把 Channel 从 EventLoop 中彻底移除。
    acceptChannel_.remove();        // 调用EventLoop->removeChannel => Poller->removeChannel 把Poller的ChannelMap对应的部分删除
}

void Acceptor::listen() // 作用是启动监听过程：套接字进入监听状态，准备接受新的连接。并启动 Channel 的读事件监听。
{
    // 每个套接字（如监听套接字、已连接套接字）对应一个 Channel, Channel为不同事件（读、写、异常）绑定回调函数。
    listenning_ = true; // 1. 更新状态为“正在监听”
    
    // 2. 调用底层 socket 的 listen() 函数。
    // 这是真正的系统调用，让内核开始为这个 socket 维护一个等待连接的队列。
    acceptSocket_.listen();
    
    // 3. 关键一步：在 Channel 上启用读事件。

    // 通过调用 EventLoop 的 updateChannel 方法，将 acceptChannel_ 注册到 Poller 中。
    // 这一步是将 acceptChannel_ 添加到 Poller 中，Poller 会监听这个 Channel 上的事件。
    // 当有新连接到来时，Poller 会通知 EventLoop，EventLoop 再调用 acceptChannel_ 的 handleEvent() 方法，
    // 进而触发我们在 acceptChannel_ 上设置的读事件回调函数 handleRead()。
    acceptChannel_.enableReading();
}

// listenfd有事件发生了，就是有新用户连接了
// 在监听到可读事件时，handleRead() 函数会被调用。
void Acceptor::handleRead()
{
    InetAddress peerAddr; // peerAddr 用于存储新连接的客户端地址信息。

    // 调用 acceptSocket_ 的 accept 方法，这会调用底层的 accept() 系统调用。
    // 因为 socket 是非阻塞的，这个调用会立即返回。
    // - 如果有新连接，它返回一个新的用于通信的 socket 文件描述符 (connfd)，并填充 peerAddr。
    // - 如果没有新连接（可能发生在边缘触发模式下），它会返回-1。
    int connfd = acceptSocket_.accept(&peerAddr);
    if (connfd >= 0) 
    {
        if (NewConnectionCallback_) // 检查是否设置了新连接的回调函数
        {
            // 调用回调函数，把新连接的 connfd 和客户端地址传过去。
            // 这就是“交接”工作。Acceptor 的任务到此完成一半。
            // 回调函数（通常在 TcpServer 中定义）会接手这个 connfd，
            // 将它打包成一个新的 TcpConnection，并分配给一个 subLoop 去处理。
            NewConnectionCallback_(connfd, peerAddr); // 轮询找到subLoop 唤醒并分发当前的新客户端的Channel
        }
        else // 如果没有设置回调，没人处理这个新连接，只能关闭它。
        {
            ::close(connfd);
        }
    }
    else // 如果 accept() 返回 -1，表示没有新连接或发生了错误
    {
        LOG_ERROR<<"accept Err";

        // 特殊错误处理：EMFILE
        // 这个错误意味着进程打开的文件描述符已经达到了系统上限。
        // 这是一个严重的资源耗尽问题。
        if (errno == EMFILE)
        {
            LOG_ERROR<<"sockfd reached limit";
        }
    }
}