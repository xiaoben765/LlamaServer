#include <errno.h>
#include <unistd.h>
#include <string.h>

#include <EPollPoller.h>
#include <Logger.h>
#include <Channel.h>

const int kNew = -1;    // 某个channel还没添加至Poller          // channel的成员index_初始化为-1
const int kAdded = 1;   // 某个channel已经添加至Poller
const int kDeleted = 2; // 某个channel已经从Poller删除

EPollPoller::EPollPoller(EventLoop *loop)
    : Poller(loop) // 调用基类Poller的构造函数Poller(loop)
    , epollfd_(::epoll_create1(EPOLL_CLOEXEC)) // 创建一个epoll实例，返回的文件描述符是epollfd_
    , events_(kInitEventListSize) // vector<epoll_event>(16)，表示能够容纳16个事件的epoll_event结构体
    // 成员初始化列表包括：epoll所属的事件循环指针loop，epollfd_的文件描述符，events_的初始大小为16
{
    if (epollfd_ < 0)
    {
        LOG_FATAL<<"epoll_create error:%d \n"<<errno;
    }
}

EPollPoller::~EPollPoller()
{
    ::close(epollfd_); // 关闭epoll实例的文件描述符，释放资源
}

// 输入参数timeoutMs是超时时间，activeChannels是输出参数，用于存储发生事件的Channel列表
Timestamp EPollPoller::poll(int timeoutMs, ChannelList *activeChannels)
{
    // 由于频繁调用poll 实际上应该用LOG_DEBUG输出日志更为合理 当遇到并发场景 关闭DEBUG日志提升效率
    LOG_INFO<<"fd total count:"<<channels_.size();

    // events_是一个vector<epoll_event>，用于存储epoll_wait返回的事件
    // 调用::epoll_wait函数等待事件发生，参数包括epollfd_、events_的起始地址、events_的大小和超时时间timeoutMs
    /*
        epollfd_: 告诉内核要监听哪个 epoll 实例，epoll示例指的是哪一个轮询器，此处是 EPollPoller
        &*events_.begin(): 获取 events_ 这个 vector 底层数组的起始地址。epoll_wait 会把结果写到这个地址。
        static_cast<int>(events_.size()): 告诉内核这个数组有多大，最多能接收多少个事件。
        timeoutMs: 超时时间（毫秒）。如果在这段时间内没有任何事件，epoll_wait 就会返回 0。如果设为 -1，它会一直阻塞直到有事件发生。
        返回值 numEvents: 发生了事件的连接数量。
    */
    int numEvents = ::epoll_wait(epollfd_, &*events_.begin(), static_cast<int>(events_.size()), timeoutMs);
    int saveErrno = errno;
    Timestamp now(Timestamp::now());

    if (numEvents > 0)
    {
        LOG_INFO<<"events happend"<<numEvents; // LOG_DEBUG最合理
        fillActiveChannels(numEvents, activeChannels); // 将发生事件的Channel指针存储到activeChannels中
        if (numEvents == events_.size()) // 扩容操作, 如果发生的事件数量等于当前events_的大小
        {
            // 如果这一次返回的事件数刚好把我们准备的数组填满了，说明连接可能很繁忙，
            // 下次可能会有更多事件。我们主动把数组容量翻倍，避免 epoll_wait 因为空间不足而无法返回所有事件。
            events_.resize(events_.size() * 2);
        }
    }
    else if (numEvents == 0)
    {
        LOG_DEBUG<<"timeout!";
    }
    else // numEvents < 0，说明发生了错误
    {
        if (saveErrno != EINTR) // EINTR 表示被信号中断了
        {
            // 如果不是因为信号中断导致的错误，才记录错误日志
            // 这里的errno是epoll_wait调用失败时的错误码
            // 如果是EINTR，表示被信号中断了，可以忽略这个错误
            // 其他错误则需要记录日志
            LOG_ERROR<<"EPollPoller::poll() error! errno="<<saveErrno;
        }
        else
        {
            errno = saveErrno; // 恢复errno的值，避免被其他函数调用修改
            LOG_ERROR<<"EPollPoller::poll() error!";
        }
    }
    return now;
}

// channel update remove => EventLoop updateChannel removeChannel => Poller updateChannel removeChannel
// 更新Poller中的channel状态，主要包括添加、修改和删除操作
void EPollPoller::updateChannel(Channel *channel)
{
    const int index = channel->index(); // 获取channel在Poller中的索引位置
    // index的值有三种：kNew, kAdded, kDeleted
    // kNew表示channel还没有添加到Poller中，kAdded表示channel已经添加到Poller中，kDeleted表示channel已经从Poller中删除

    LOG_INFO<<"func =>"<<"fd"<<channel->fd()<<"events="<<channel->events()<<"index="<<index; 
    // 打印输出 channel的fd、事件类型和索引位置
    

    if (index == kNew || index == kDeleted) // 如果channel是新添加的或者已经被删除了
    {
        if (index == kNew) // 说明Poller中没有注册过这个channel
        {
            int fd = channel->fd(); // 获取channel的文件描述符
            channels_[fd] = channel; // 将channel添加到channels_中，key是fd，value是channel指针
        }
        else // index == kDeleted
        { 
        }

        // 这里的逻辑是：如果channel是新添加的或者之前被删除了，那么我们需要将它添加到Poller中
        // 跟前面的区别在于，这里是重新添加channel到Poller中
        channel->set_index(kAdded); // 设置channel的索引位置为kAdded，KAdded表示channel已经添加到Poller中
        update(EPOLL_CTL_ADD, channel); // 调用update函数，向epoll实例中添加channel，EPOLL_CTL_ADD表示添加操作
    }
    else // channel已经在Poller中注册过了，需要更新或者删除它
    {
        int fd = channel->fd(); // 获取channel的文件描述符
        if (channel->isNoneEvent()) // 如果channel没有注册任何事件，events == kNoneEvent
        {
            update(EPOLL_CTL_DEL, channel); // 调用update函数，向epoll实例中删除channel，EPOLL_CTL_DEL表示删除操作
            channel->set_index(kDeleted); // 标记channel为已删除状态
        }
        else
        {
            update(EPOLL_CTL_MOD, channel); // 调用update函数，向epoll实例中修改channel，EPOLL_CTL_MOD表示修改操作
        }
    }
}

// 从Poller中删除channel
void EPollPoller::removeChannel(Channel *channel)
{
    int fd = channel->fd(); // 获取对应的文件描述符
    channels_.erase(fd); // 从channels_中删除channel，key是fd，value是channel指针

    LOG_INFO<<"removeChannel fd="<<fd;

    int index = channel->index(); // 更新channel的索引信息
    if (index == kAdded) // 如果是已添加状态
    {
        update(EPOLL_CTL_DEL, channel); // 调用update函数，向epoll实例中删除channel，EPOLL_CTL_DEL表示删除操作
    }
    channel->set_index(kNew); // 将channel的索引位置设置为kNew，表示channel还没有添加到Poller中
}

// 填写活跃的连接
void EPollPoller::fillActiveChannels(int numEvents, ChannelList *activeChannels) const
{
    for (int i = 0; i < numEvents; ++i)
    {   
        // 这部分的作用是：当发现某个事件的文件描述符有动静，请把channel 指针原封不动的返还<参考update()函数>
        // events_[i]是epoll_wait返回的事件列表中的第i个事件
        // events_[i].data.ptr是指向Channel对象的指针，表示发生事件的Channel
        // events_[i].events是发生的事件类型，如EPOLLIN、EPOLLOUT等
        // 将发生事件的Channel指针存储到activeChannels中
        // 这里的events_是EPollPoller类的成员变量，存储了epoll_wait返回的事件列表
        // events_是一个vector<epoll_event>，每个元素表示一个发生的事件
        Channel *channel = static_cast<Channel *>(events_[i].data.ptr);
        channel->set_revents(events_[i].events);
        activeChannels->push_back(channel); // EventLoop就拿到了它的Poller给它返回的所有发生事件的channel列表了
    }
}

// 更新channel通道信息 其实就是调用epoll_ctl add/mod/del
// 输入参数operation表示操作类型，channel是要更新的Channel对象
// operation可以是EPOLL_CTL_ADD（添加）、EPOLL_CTL_MOD（修改）或EPOLL_CTL_DEL（删除）
void EPollPoller::update(int operation, Channel *channel)
{
    epoll_event event; // epoll_event是epoll_wait返回的事件类型, 就是轮询到的事件
    ::memset(&event, 0, sizeof(event)); // 为什么要清空？确保event结构体的所有字段都是初始化状态
    // meset函数用于将event结构体的内存区域全部设置为0，避免未初始化的字段影响后续操作

    int fd = channel->fd(); // 获取channel的文件描述符

    // 下面是设置epoll_event结构体的各个字段，包括事件类型、数据以及数据指针
    event.events = channel->events(); // 设置要监听的事件类型，channel->events()返回的是Poller感兴趣的事件类型，如EPOLLIN、EPOLLOUT等
    event.data.fd = fd; // 设置事件数据，这里使用文件描述符fd作为数据
    event.data.ptr = channel; // 设置事件数据指针，指向Channel对象，这样在epoll_wait返回时可以通过data.ptr获取到对应的Channel

    // epoll_ctl的作用是向epoll实例中添加、修改或删除要监听的连接
    // epoll_ctl的第一个参数是epollfd_，表示要操作的epoll实例的文件描述符
    // 第二个参数是operation，表示要执行的操作类型（添加、修改或删除） 
    // 第三个参数是fd，表示要操作的文件描述符
    // 第四个参数是event，表示要添加或修改的事件信息
    // 如果epoll_ctl操作失败，打印错误日志
    // 注意：如果operation是EPOLL_CTL_DEL，表示删除操作，此时event参数可以忽略
    if (::epoll_ctl(epollfd_, operation, fd, &event) < 0)
    {
        if (operation == EPOLL_CTL_DEL)
        {
            LOG_ERROR<<"epoll_ctl del error:"<<errno;
        }
        else // operation是EPOLL_CTL_ADD或EPOLL_CTL_MOD
        {   
            LOG_FATAL<<"epoll_ctl add/mod error:"<<errno;
        }
    }
}