#include <functional>
#include <string>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <netinet/tcp.h>
#include <sys/sendfile.h>
#include <fcntl.h> // for open
#include <unistd.h> // for close

#include <TcpConnection.h>
#include <Logger.h>
#include <Socket.h>
#include <Channel.h>
#include <EventLoop.h>

static EventLoop *CheckLoopNotNull(EventLoop *loop)
{
    if (loop == nullptr)
    {
        LOG_FATAL<<" mainLoop is null!"; // 如果 loop 是空的，记录致命错误并终止程序
    }
    return loop;
}

TcpConnection::TcpConnection(EventLoop *loop,
                             const std::string &nameArg,
                             int sockfd,
                             const InetAddress &localAddr,
                             const InetAddress &peerAddr)
    : loop_(CheckLoopNotNull(loop)) // 初始化 EventLoop，并检查不为空
    , name_(nameArg)                 // 初始化连接名称
    , state_(kConnecting)            // 初始化状态为“正在连接”
    , reading_(true)                 // 初始化为正在读取
    , socket_(new Socket(sockfd))    // 创建一个 Socket 对象来管理 sockfd
    , channel_(new Channel(loop, sockfd)) // 为此连接创建一个 Channel
    , localAddr_(localAddr)          // 本地地址
    , peerAddr_(peerAddr)            // 对方（客户端）地址
    , highWaterMark_(64 * 1024 * 1024) // 设置“高水位线”为 64MB
{
    // 下面给channel设置相应的回调函数 poller给channel通知感兴趣的事件发生了 channel会回调相应的回调函数
    channel_->setReadCallback(
        std::bind(&TcpConnection::handleRead, this, std::placeholders::_1));
    channel_->setWriteCallback(
        std::bind(&TcpConnection::handleWrite, this));
    channel_->setCloseCallback(
        std::bind(&TcpConnection::handleClose, this));
    channel_->setErrorCallback(
        std::bind(&TcpConnection::handleError, this));

    LOG_INFO<<"TcpConnection::ctor:["<<name_.c_str()<<"]at fd="<<sockfd;
    socket_->setKeepAlive(true);
}

TcpConnection::~TcpConnection()
{
    LOG_INFO<<"TcpConnection::dtor["<<name_.c_str()<<"]at fd="<<channel_->fd()<<"state="<<(int)state_;
}

void TcpConnection::send(const std::string &buf)
{
    if (state_ == kConnected) // 只有在连接状态下才能发送
    {
        if (loop_->isInLoopThread()) // 判断当前调用 send 的线程是否就是管理此连接的 I/O 线程
        {
            sendInLoop(buf.c_str(), buf.size()); // 是，直接发送。参数是数据的指针和长度
        }
        else // 如果是其他线程（比如工作线程）要发送数据
        {
            // 不是，不能直接发送，因为会产生线程竞争。
            // 把它作为一个任务，交给 EventLoop，让它在自己的线程里去执行 sendInLoop
            loop_->runInLoop(
                std::bind(&TcpConnection::sendInLoop, this, buf.c_str(), buf.size()));
        }
    }
}

/**
 * 发送数据 应用写的快 而内核发送数据慢 需要把待发送数据写入缓冲区，而且设置了水位回调
 **/
void TcpConnection::sendInLoop(const void *data, size_t len)
{
    ssize_t nwrote = 0;
    size_t remaining = len;
    bool faultError = false; // faultError表示发送数据时是否发生错误

    if (state_ == kDisconnected) // 之前调用过该connection的shutdown 不能再进行发送了
    {
        LOG_ERROR<<"disconnected, give up writing";
    }

    // 如果发送缓冲区是空的，并且 channel 没有在监听“可写”事件
    if (!channel_->isWriting() && outputBuffer_.readableBytes() == 0)
    {
        // 先试着直接用 write 发送数据。如果网络不拥堵，一次就能发完，效率最高。
        nwrote = ::write(channel_->fd(), data, len); // 直接调用write系统调用把数据写入到内核发送缓冲区
        if (nwrote >= 0) // 发送成功
        {
            remaining = len - nwrote; // 计算剩余未发送的数据长度
            if (remaining == 0 && writeCompleteCallback_) // 如果数据全部发送完成，并且设置了写完成回调
            {
                // 既然在这里数据全部发送完成，就不用再给channel设置epollout事件了
                loop_->queueInLoop(
                    std::bind(writeCompleteCallback_, shared_from_this()));
            }
        }
        else // nwrote < 0 
        {
            nwrote = 0; // nwrote = 0表示没有发送出去数据
            if (errno != EWOULDBLOCK) // EWOULDBLOCK表示非阻塞情况下没有数据后的正常返回 等同于EAGAIN
            {
                LOG_ERROR<<"TcpConnection::sendInLoop";
                if (errno == EPIPE || errno == ECONNRESET) // SIGPIPE RESET
                { // 两个错误类型表示连接已经断开 或者 连接被对端重置
                    faultError = true;  // 会触发后续的连接关闭逻辑
                }
            }
        }
    }
    /**
     * 说明当前这一次write并没有把数据全部发送出去 剩余的数据需要保存到缓冲区当中
     * 然后给channel注册EPOLLOUT事件，Poller发现tcp的发送缓冲区有空间后会通知
     * 相应的sock->channel，调用channel对应注册的writeCallback_回调方法，
     * channel的writeCallback_实际上就是TcpConnection设置的handleWrite回调，
     * 把发送缓冲区outputBuffer_的内容全部发送完成
     **/
    if (!faultError && remaining > 0)
    {
        // 目前发送缓冲区剩余的待发送的数据的长度
        size_t oldLen = outputBuffer_.readableBytes();
        if (oldLen + remaining >= highWaterMark_ && oldLen < highWaterMark_ && highWaterMarkCallback_)
        {   // 如果发送缓冲区的长度超过了高水位线 && 之前没有触发过高水位回调 && 设置了高水位回调

            loop_->queueInLoop(
                std::bind(highWaterMarkCallback_, shared_from_this(), oldLen + remaining));
            
            // 为什么要判断 oldLen < highWaterMark_ ?
            // 因为如果 oldLen >= highWaterMark_，说明已经触发过高水位回调了，不需要再次触发
            // 这里的回调是为了通知上层应用发送缓冲区的长度超过了高水位线
            // 这时上层应用可以采取措施，比如暂停发送数据，或者清理发送缓冲区
        }

        // 底层数据结构是vector<char>，可以直接追加数据
        outputBuffer_.append((char *)data + nwrote, remaining); // 把剩余未发送的数据追加到发送缓冲区outputBuffer_中
        if (!channel_->isWriting()) // 如果channel没有注册写事件，也就是数据已经全部发送完了
        {
            channel_->enableWriting(); // 这里一定要注册channel的写事件 否则poller不会给channel通知epollout
        }
    }
}

void TcpConnection::shutdown() // shutdown中调用shutdownInLoop，是为了在当前所属的loop中执行关闭操作
{
    if (state_ == kConnected) // 如果是已连接状态
    {
        setState(kDisconnecting);
        loop_->runInLoop(
            std::bind(&TcpConnection::shutdownInLoop, this)); // 在当前所属的loop中执行shutdownInLoop
    }
}

void TcpConnection::shutdownInLoop()
{
    if (!channel_->isWriting()) // 已经没有写事件了，说明当前outputBuffer_的数据全部向外发送完成
    {
        socket_->shutdownWrite(); // 关闭socket的写端
    }
}

// 连接建立
void TcpConnection::connectEstablished()
{
    setState(kConnected);
    channel_->tie(shared_from_this()); // 绑定channel的shared_ptr到TcpConnection上，防止channel在回调中被销毁
    channel_->enableReading(); // 向poller注册channel的EPOLLIN读事件

    // 新连接建立 执行回调
    connectionCallback_(shared_from_this()); // 这里的shared_from_this()是获取当前TcpConnection的智能指针
}
// 连接销毁
void TcpConnection::connectDestroyed()
{
    if (state_ == kConnected)
    {
        setState(kDisconnected);
        channel_->disableAll(); // 把channel的所有感兴趣的事件从poller中删除掉
        connectionCallback_(shared_from_this()); // 连接回调, 通知TcpServer有连接断开了
    }

    // 不管是否连接成功 都要把channel从poller中删除掉
    channel_->remove(); // 把channel从poller中删除掉
}

// 读是相对服务器而言的 当对端客户端有数据到达 服务器端检测到EPOLLIN 就会触发该fd上的回调 handleRead取读走对端发来的数据
void TcpConnection::handleRead(Timestamp receiveTime)
{
    int savedErrno = 0; // 保存错误码
    ssize_t n = inputBuffer_.readFd(channel_->fd(), &savedErrno); // 从channel的fd上读取数据到inputBuffer_缓冲区中

    if (n > 0) // 有数据到达
    {
        // 已建立连接的用户有可读事件发生了 调用用户传入的回调操作onMessage shared_from_this就是获取了TcpConnection的智能指针
        // 从inputBuffer_缓冲区中读取数据，shared_from_this()返回当前TcpConnection的shared_ptr
        messageCallback_(shared_from_this(), &inputBuffer_, receiveTime);
    }
    else if (n == 0) // 客户端断开
    {
        handleClose(); // 处理连接关闭事件，调用handleClose()方法
    }
    else // 出错了
    {
        errno = savedErrno;
        LOG_ERROR<<"TcpConnection::handleRead";
        handleError();
    }
}

// 处理写事件，往输出缓冲区outputBuffer_中写入数据
void TcpConnection::handleWrite()
{
    if (channel_->isWriting()) // 如果通道注册了写事件
    {
        int savedErrno = 0;
        ssize_t n = outputBuffer_.writeFd(channel_->fd(), &savedErrno);
        if (n > 0) // 说明长度为n的数据写入了输出缓冲区
        {
            outputBuffer_.retrieve(n);//从缓冲区读取reable区域的数据移动readindex下标
            if (outputBuffer_.readableBytes() == 0)  // 输出缓冲区中的数据已经全部发送完了
            {
                channel_->disableWriting(); // 禁用写事件，表示没有数据需要发送了
                if (writeCompleteCallback_) // 如果设置了写完成回调
                {
                    // TcpConnection对象在其所在的subloop中 向pendingFunctors_中加入回调
                    // writeCompleteCallback_表明写操作已经完成
                    loop_->queueInLoop(
                        std::bind(writeCompleteCallback_, shared_from_this()));
                }
                if (state_ == kDisconnecting) // 如果写完数据后，连接状态是正在断开
                {
                    shutdownInLoop(); // 在当前所属的loop中把TcpConnection删除掉
                }
            }
        }
        else
        {
            LOG_ERROR<<"TcpConnection::handleWrite";
        }
    }
    else
    {
        LOG_ERROR<<"TcpConnection fd="<<channel_->fd()<<"is down, no more writing"; // 如果通道没有注册写事件，说明连接已经断开了
    }
}

void TcpConnection::handleClose()
{
    LOG_INFO<<"TcpConnection::handleClose fd="<<channel_->fd()<<"state="<<(int)state_;
    setState(kDisconnected);
    channel_->disableAll(); // 禁用所有事件，表示连接已经关闭

    // 为什么要创建一个TcpConnectionPtr智能指针？
    // 因为在handleClose中需要调用connectionCallback_和closeCallback_，这两个回调函数需要一个TcpConnectionPtr参数。
    // 这个智能指针可以确保在回调函数执行完毕后，TcpConnection对象不会被销毁，直到所有引用都释放。
    // 这样可以避免在回调函数中访问已经被销毁的TcpConnection对象导致的未定义行为。

    // 为什么会出现访问已经被销毁的TcpConnection对象？
    // 因为在handleClose中，TcpConnection对象可能已经被connectionCallback_销毁了，但回调函数handleClose仍然需要访问它，用来结束函数流程
    TcpConnectionPtr connPtr(shared_from_this()); // 创建一个TcpConnectionPtr智能指针，指向当前的TcpConnection对象，增加引用计数
    connectionCallback_(connPtr); // 连接回调，通知TcpServer有连接断开了
    closeCallback_(connPtr);      // 执行关闭连接的回调 执行的是TcpServer::removeConnection回调方法   // must be the last line
}

void TcpConnection::handleError()
{
    int optval; // 用于存储SO_ERROR的值
    socklen_t optlen = sizeof optval; // 获取SO_ERROR的长度
    int err = 0;
    if (::getsockopt(channel_->fd(), SOL_SOCKET, SO_ERROR, &optval, &optlen) < 0)
    {
        // getsockopt函数获取套接字选项
        // SOL_SOCKET: 套接字选项级别
        // SO_ERROR: 获取套接字的错误状态
        // optval: 用于存储错误码
        // optlen: 用于存储错误码的长度
        err = errno;
    }
    else
    {
        err = optval; // 如果getsockopt成功，optval将包含SO_ERROR的值
    }
    LOG_ERROR<<"TcpConnection::handleError name:"<<name_.c_str()<<"- SO_ERROR:%"<<err;
}

// 新增的零拷贝发送函数
// 什么是零拷贝？
// 零拷贝是一种优化技术，旨在减少数据在内存中的复制次数，从而提高数据传输效率。
// 在这里，sendFileInLoop函数使用了sendfile系统调用，sendfile将数据直接在内核空间中从文件缓冲区移动到socket缓冲区，避免了中间的用户空间拷贝。
// 这样可以减少数据在内核和用户空间之间的复制，提高性能，尤其是在处理大文件传输时。      
void TcpConnection::sendFile(int fileDescriptor, off_t offset, size_t count) {
    if (connected()) { // 检查连接状态，如果是已连接
        if (loop_->isInLoopThread()) { // 判断当前线程是否是loop循环的线程
            sendFileInLoop(fileDescriptor, offset, count); // 是在本线程中，就直接执行sendFileInLoop
        }else{ // 如果不是，则唤醒运行这个TcpConnection的线程执行Loop循环
            loop_->runInLoop(
                std::bind(&TcpConnection::sendFileInLoop, shared_from_this(), fileDescriptor, offset, count));
        }
    } else {
        LOG_ERROR<<"TcpConnection::sendFile - not connected";
    }
}

// 在事件循环中执行sendfile
void TcpConnection::sendFileInLoop(int fileDescriptor, off_t offset, size_t count) {
    ssize_t bytesSent = 0; // 发送了多少字节数
    size_t remaining = count; // 还要多少数据要发送
    bool faultError = false; // 错误的标志位

    if (state_ == kDisconnecting) { // 表示此时连接已经断开就不需要发送数据了
        LOG_ERROR<<"disconnected, give up writing";
        return;
    }

    // 当前没有在排队等待“可写”事件，即发送通道是畅通的
    // 且outputBuffer缓冲区中没有数据，应用层缓冲区是空的，没有任何旧数据积压着等待发送
    // 这时可以直接使用sendfile系统调用来发送文件数据，而不是使用自定义的缓冲区发送
    if (!channel_->isWriting() && outputBuffer_.readableBytes() == 0) {
        bytesSent = sendfile(socket_->fd(), fileDescriptor, &offset, remaining);
        if (bytesSent >= 0) {
            remaining -= bytesSent; // 更新剩余未发送的数据长度
            if (remaining == 0 && writeCompleteCallback_) {
                // remaining为0意味着数据正好全部发送完，就不需要给其设置写事件的监听。
                loop_->queueInLoop(std::bind(writeCompleteCallback_, shared_from_this()));
            }
        } else { // bytesSent < 0
            if (errno != EWOULDBLOCK) { // 如果是非阻塞没有数据返回错误这个是正常显现等同于EAGAIN，否则就异常情况
                LOG_ERROR<<"TcpConnection::sendFileInLoop";
            }
            if (errno == EPIPE || errno == ECONNRESET) {
                faultError = true; // 如果是EPIPE或ECONNRESET，表示连接已经断开或被对端重置
                // 这时需要关闭连接，后续会调用handleClose()来处理
            }
        }
    }

    // 处理剩余数据
    // 两种情况：没有发送完，或者没办法走sendfile发送数据
    // 如果在上面remaining == 0 后面的else分支中继续处理剩余数据，那就没办法处理 ”不能使用 sendfile发送数据“ 的方式！
    if (!faultError && remaining > 0) { // 如果没有发生错误，并且还有剩余数据需要发送
        // 将一个绑定了剩余发送任务的新函数放入事件循环，等待下一次执行
        loop_->queueInLoop(
            std::bind(&TcpConnection::sendFileInLoop, shared_from_this(), fileDescriptor, offset, remaining));
    }
}