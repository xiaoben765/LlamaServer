#include <sys/eventfd.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <memory>

#include <EventLoop.h>
#include <Logger.h>
#include <Channel.h>
#include <Poller.h>

// 防止一个线程创建多个EventLoop
thread_local EventLoop *t_loopInThisThread = nullptr;

// 定义默认的Poller IO复用接口的超时时间
const int kPollTimeMs = 10000; // 10000毫秒 = 10秒钟

/* 创建线程之后主线程和子线程谁先运行是不确定的。
 * 通过一个eventfd在线程之间传递数据的好处是多个线程无需上锁就可以实现同步。
 * eventfd支持的最低内核版本为Linux 2.6.27,在2.6.26及之前的版本也可以使用eventfd，但是flags必须设置为0。
 * 函数原型：
 *     #include <sys/eventfd.h>
 *     int eventfd(unsigned int initval, int flags);
 * 参数说明：
 *      initval,初始化计数器的值。
 *      flags, EFD_NONBLOCK,设置socket为非阻塞。
 *             EFD_CLOEXEC，执行fork的时候，在父进程中的描述符会自动关闭，子进程中的描述符保留。
 * 场景：
 *     eventfd可以用于同一个进程之中的线程之间的通信。
 *     eventfd还可以用于同亲缘关系的进程之间的通信。
 *     eventfd用于不同亲缘关系的进程之间通信的话需要把eventfd放在几个进程共享的共享内存中（没有测试过）。
 */
// 创建wakeupfd 用来notify唤醒subReactor处理新来的channel
int createEventfd()
{
    int evtfd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (evtfd < 0)
    {
        LOG_FATAL<<"eventfd error:%d"<<errno;
    }
    return evtfd;
}

EventLoop::EventLoop()
    : looping_(false)
    , quit_(false)
    , callingPendingFunctors_(false)
    , threadId_(CurrentThread::tid())
    , poller_(Poller::newDefaultPoller(this))
    , wakeupFd_(createEventfd())
    , wakeupChannel_(new Channel(this, wakeupFd_))
{
    LOG_DEBUG<<"EventLoop created"<<this<<"in thread"<<threadId_;
    if (t_loopInThisThread)
    {
        LOG_FATAL<<"Another EventLoop"<<t_loopInThisThread<<"exists in this thread "<<threadId_;
    }
    else
    {
        t_loopInThisThread = this;
    }
    
    wakeupChannel_->setReadCallback(
        std::bind(&EventLoop::handleRead, this)); // 设置wakeupfd的事件类型以及发生事件后的回调操作
    
    wakeupChannel_->enableReading(); // 每一个EventLoop都将监听wakeupChannel_的EPOLL读事件了
}
EventLoop::~EventLoop()
{
    wakeupChannel_->disableAll(); // 给Channel移除所有感兴趣的事件
    wakeupChannel_->remove();     // 把Channel从EventLoop上删除掉
    ::close(wakeupFd_);
    t_loopInThisThread = nullptr;
}

// 开启事件循环
void EventLoop::loop()
{
    looping_ = true;
    quit_ = false;

    LOG_INFO<<"EventLoop start looping";

    while (!quit_)
    {
        activeChannels_.clear(); // 清空上一次poller_->poll()返回的发生事件的Channels列表
        pollRetureTime_ = poller_->poll(kPollTimeMs, &activeChannels_); // Poller::poll()阻塞等待事件发生，返回发生事件的Channels列表
        for (Channel *channel : activeChannels_)
        {
            // Poller监听哪些channel发生了事件 然后上报给EventLoop 通知channel处理相应的事件
            channel->handleEvent(pollRetureTime_);
        }
        /**
         * 执行当前EventLoop事件循环需要处理的回调操作 对于线程数 >=2 的情况 IO线程 mainloop(mainReactor) 主要工作：
         * accept接收连接 => 将accept返回的connfd打包为Channel => TcpServer::newConnection通过轮询将TcpConnection对象分配给subloop处理
         *
         * mainloop调用queueInLoop将回调加入subloop（该回调需要subloop执行 但subloop还在poller_->poll处阻塞） queueInLoop通过wakeup将subloop唤醒
         **/
        doPendingFunctors(); // 执行当前EventLoop需要执行的回调操作
    }
    LOG_INFO<<"EventLoopstop looping";
    looping_ = false;
}

/**
 * 退出事件循环
 * 1. 如果loop在自己的线程中调用quit成功了 说明当前线程已经执行完毕了loop()函数的poller_->poll并退出
 * 2. 如果不是当前EventLoop所属线程中调用quit退出EventLoop 需要唤醒EventLoop所属线程的epoll_wait
 *
 * 比如在一个subloop(worker)中调用mainloop(IO)的quit时 需要唤醒mainloop(IO)的poller_->poll 让其执行完loop()函数
 *
 * ！！！ 注意： 正常情况下 mainloop负责请求连接 将回调写入subloop中 通过生产者消费者模型即可实现线程安全的队列
 * ！！！       但是muduo通过wakeup()机制 使用eventfd创建的wakeupFd_ notify 使得mainloop和subloop之间能够进行通信
 **/
void EventLoop::quit()
{
    quit_ = true;

    if (!isInLoopThread()) // 为了及时响应退出，因为有可能此时loop()函数正在阻塞在poller_->poll()中
    {
        wakeup(); // 唤醒EventLoop所在线程的poller_->poll()函数 让其退出阻塞状态
    }
}

// 在当前loop中执行cb
void EventLoop::runInLoop(Functor cb) // 在当前EventLoop所属的线程中执行回调操作
{
    if (isInLoopThread()) // 如果当前EventLoop所属的线程就是当前线程
    {
        cb(); // 直接执行回调操作
        // 注意：如果cb()中又调用了queueInLoop()就会导致死锁 因为cb()是在当前EventLoop所属的线程中执行的
    }
    else // 在非当前EventLoop线程中执行cb，就需要唤醒EventLoop所在线程执行cb
    {
        queueInLoop(cb);
    }
}

// 把cb放入队列中 唤醒loop所在的线程执行cb
void EventLoop::queueInLoop(Functor cb) // 在非当前EventLoop所属的线程中执行回调操作
{
    {
        std::unique_lock<std::mutex> lock(mutex_); // 锁住当前EventLoop的mutex_互斥锁，保护pendingFunctors_的线程安全操作
        pendingFunctors_.emplace_back(cb); // 将回调操作放入到当前EventLoop的pendingFunctors_中
        // 注意：如果在当前loop线程中执行回调操作 需要唤醒loop所在线程
        //       如果在非当前loop线程中执行回调操作 需要唤醒loop所在线程
        //       让loop()函数的poller_->poll()不再阻塞
        //       让loop()函数的doPendingFunctors()能够执行pendingFunctors_中的回调操作
    }

    /**
     * || callingPendingFunctors的意思是 当前loop正在执行回调中 但是loop的pendingFunctors_中又加入了新的回调 需要通过wakeup写事件
     * 唤醒相应的需要执行上面回调操作的loop的线程 让loop()下一次poller_->poll()不再阻塞（阻塞的话会延迟前一次新加入的回调的执行），然后
     * 继续执行pendingFunctors_中的回调函数
     **/
    if (!isInLoopThread() || callingPendingFunctors_) // 如果当前EventLoop所属的线程不是当前线程 或者当前EventLoop正在执行回调操作
    {
        wakeup(); // 唤醒loop所在线程
    }
}

void EventLoop::handleRead() // 处理wakeupFd_的读事件
{
    uint64_t one = 1; // 读取wakeupFd_中的数据
    ssize_t n = read(wakeupFd_, &one, sizeof(one)); // handleRead 的 read 操作是重置“门铃”状态, 自动把这个计数器清零
    if (n != sizeof(one)) // 如果读取的字节数不是8字节
    {
        LOG_ERROR<<"EventLoop::handleRead() reads"<<n<<"bytes instead of 8";
    }
}

// 用来唤醒loop所在线程 向wakeupFd_写一个数据 wakeupChannel就发生读事件 当前loop线程就会被唤醒
void EventLoop::wakeup() // 向wakeupFd_写入一个数据 让poller_->poll()不再阻塞
{
    uint64_t one = 1; // 向wakeupFd_写入一个数据
    ssize_t n = write(wakeupFd_, &one, sizeof(one)); // 向wakeupFd_写入8字节数据
    if (n != sizeof(one))
    {
        LOG_ERROR<<"EventLoop::wakeup() writes"<<n<<"bytes instead of 8";
    }
}

// EventLoop的方法 => Poller的方法
// 这体现了一个非常重要的软件设计原则：封装 (Encapsulation) 和 委托 (Delegation)。
void EventLoop::updateChannel(Channel *channel)
{
    poller_->updateChannel(channel); // 更新/添加一个监控任务
}

void EventLoop::removeChannel(Channel *channel)
{
    poller_->removeChannel(channel); // 从Poller中删除一个监控任务
}

bool EventLoop::hasChannel(Channel *channel)
{
    return poller_->hasChannel(channel); // 判断Poller中是否有该Channel
}

void EventLoop::doPendingFunctors()
{
    std::vector<Functor> functors; // 定义一个临时的回调操作容器 用来存储当前EventLoop需要执行的回调操作
    callingPendingFunctors_ = true; // 标识当前EventLoop正在执行回调操作

    {
        std::unique_lock<std::mutex> lock(mutex_);
        functors.swap(pendingFunctors_); // 将当前EventLoop的pendingFunctors_中的回调操作交换到临时容器functors中
        // swap 仅仅是交换了两个 vector 内部的几个指针（指向数据存储区、大小、容量的指针）。
        // 交换的方式减少了锁的临界区范围 提升效率 同时避免了死锁 如果执行functor()在临界区内 且functor()中调用queueInLoop()就会产生死锁
    }

    for (const Functor &functor : functors)
    {
        functor(); // 执行当前loop需要执行的回调操作
    }

    callingPendingFunctors_ = false;
}
