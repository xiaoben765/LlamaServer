#pragma once

#include <vector>
#include <sys/epoll.h>

#include "Poller.h"
#include "Timestamp.h"

/**
 * epoll的使用:
 * 1. epoll_create  -> 创建一个epoll实例
 * 2. epoll_ctl (add, mod, del) -> 向epoll实例中添加、修改、删除要监听的连接
 * 3. epoll_wait   -> 等待事件发生
 **/

class Channel;

class EPollPoller : public Poller // 继承自Poller类
{
public:
    // 下面的构造函数和析构函数是EPollPoller类的成员函数
    EPollPoller(EventLoop *loop);
    ~EPollPoller() override; // override 关键字表示重写基类的虚函数

    // 重写基类Poller的抽象方法
    Timestamp poll(int timeoutMs, ChannelList *activeChannels) override;
    void updateChannel(Channel *channel) override;
    void removeChannel(Channel *channel) override;

private:
    // 下面的函数是EPollPoller类的私有成员函数
    static const int kInitEventListSize = 16; // 初始事件列表大小

    // 填写活跃的连接
    void fillActiveChannels(int numEvents, ChannelList *activeChannels) const;
    // 更新channel通道 其实就是调用epoll_ctl
    void update(int operation, Channel *channel);

    // epoll_event是epoll_wait返回的事件类型, 就是轮询到的事件
    // epoll_event结构体包含了事件类型和对应的文件描述符、通道描述符
    // 事件类型包括读、写、错误等事件，文件描述符是被监听的socket或文件的标识符
    // EventList用于存储epoll_wait返回的事件列表，使用vector动态管理内存
    using EventList = std::vector<epoll_event>; // C++中可以省略struct 直接写epoll_event即可


    int epollfd_;      // epoll_create()返回的文件描述符，是epoll实例的唯一标识
    EventList events_; // 用于存放epoll_wait返回的所有发生的事件的文件描述符事件集
};