#include <sys/epoll.h>

#include <Channel.h>
#include <EventLoop.h>
#include <Logger.h>

const int Channel::kNoneEvent = 0; //空事件
const int Channel::kReadEvent = EPOLLIN | EPOLLPRI; //读事件
const int Channel::kWriteEvent = EPOLLOUT; //写事件

// EventLoop: ChannelList Poller
Channel::Channel(EventLoop *loop, int fd)
    : loop_(loop) // Channel所属的EventLoop
    , fd_(fd) // Channel所表示的文件描述符
    , events_(0) // Channel所感兴趣的事件
    , revents_(0) // Poller返回的事件
    , index_(-1) // 在Poller中的索引位置，-1表示未注册
    , tied_(false) // 是否绑定了shared_ptr对象
{
}

Channel::~Channel()
{
}

// channel的tie方法什么时候调用过?  TcpConnection => channel
/**
 * TcpConnection中注册了Channel对应的回调函数，传入的回调函数均为TcpConnection
 * 对象的成员方法，因此可以说明一点就是：Channel的结束一定晚于TcpConnection对象！
 * 此处用tie去解决TcpConnection和Channel的生命周期时长问题，从而保证了Channel对象能够在
 * TcpConnection销毁前销毁。
 **/
void Channel::tie(const std::shared_ptr<void> &obj)
{
    
    /*
        1. TcpConnection初始化时：
        - 将自己的业务逻辑函数（如handleRead）注册到Channel
        - [本质] 把函数指针或lambda存入Channel的成员变量

        2. EventLoop轮询时：
        - 发现socket有可读事件 → 通知对应Channel

        3. Channel响应时：
        - 调用自身的handleEvent()
        - [关键] 执行之前存储的回调函数（即TcpConnection的handleRead）
    */

    tie_ = obj; // 将传入的shared_ptr对象绑定到Channel上
    // Channel通过weak_ptr观察TcpConnection的生命周期，在执行回调前检查
    // 如果TcpConnection还活着 → 安全执行回调
    // 如果TcpConnection已销毁 → 不执行回调，避免空指针

    tied_ = true; // tied_ 是一个布尔标志，标记「这个 Channel 已经绑定了生命周期」。
    // 后续执行回调前，会检查这个标志，决定是否需要先验证对象是否存活。
}
//update 和remove => EpollPoller 更新channel在poller中的状态
/**
 * 当改变channel所表示的fd的events事件后，update负责再poller里面更改fd相应的事件epoll_ctl
 **/
void Channel::update()
{
    // 通过channel所属的eventloop，调用poller的相应方法，注册fd的events事件
    loop_->updateChannel(this);
}

// 在channel所属的EventLoop中把当前的channel删除掉
void Channel::remove()
{
    loop_->removeChannel(this);
}

void Channel::handleEvent(Timestamp receiveTime)
{
    if (tied_) // 如果Channel绑定了一个shared_ptr对象
    {   
        // 尝试提升weak_ptr为shared_ptr
        // 这里的提升是为了确保在执行回调时，Channel对象仍然存在
        // 如果Channel对象已经被销毁，tie_将无法提升为有效的shared_ptr
        // 通过lock()方法尝试获取绑定的shared_ptr对象        
        std::shared_ptr<void> guard = tie_.lock(); // 尝试获取绑定的shared_ptr对象
        if (guard)
        {
            handleEventWithGuard(receiveTime);
        }
        // 如果提升失败，说明 TcpConnection 对象已经销毁了，什么都不做，安全退出
    }
    else // 如果没有绑定shared_ptr对象
    {
        handleEventWithGuard(receiveTime); // 直接处理事件 
    }
}

void Channel::handleEventWithGuard(Timestamp receiveTime)
{
    LOG_INFO<<"channel handleEvent revents:"<<revents_; // 打印当前Channel的事件状态

    // 下面的事件处理逻辑是根据revents_的状态来判断发生了哪些事件，并调用相应的回调函数
    // 注意：revents_是Poller返回的具体发生的事件，可能包含多个事件的位掩码
    // 处理事件时，先检查是否绑定了shared_ptr对象，如果绑定了则使用它来保证Channel的生命周期
    // 处理事件的顺序是：关闭、错误、读、写

    // 关闭
    // 当TcpConnection对应Channel 通过shutdown 关闭写端 epoll触发EPOLLHUP
    // 这里的逻辑是：如果发生了EPOLLHUP事件<连接被关闭>，并且没有EPOLLIN事件<输入>，说明连接已经关闭
    // 这种情况下，通常是因为对端关闭了连接，或者发生了错误
    if ((revents_ & EPOLLHUP) && !(revents_ & EPOLLIN))
    {   // EPOLLHUP事件表示连接被关闭
        // EPOLLIN事件表示有数据可读，!(revents_ & EPOLLIN)表示无数据可读
        if (closeCallback_) // 在channel中注册了关闭事件的回调函数
        {
            closeCallback_(); // 调用关闭事件的回调函数，在注册时有closeCallback_ = std::move(cb);
        }
    }
    // 错误
    if (revents_ & EPOLLERR)
    {
        if (errorCallback_)
        {
            errorCallback_();
        }
    }
    // 读
    if (revents_ & (EPOLLIN | EPOLLPRI))
    {
        if (readCallback_)
        {
            readCallback_(receiveTime);
        }
    }
    // 写
    if (revents_ & EPOLLOUT)
    {
        if (writeCallback_)
        {
            writeCallback_();
        }
    }
}