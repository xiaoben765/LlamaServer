#pragma once

#include <vector>
#include <unordered_map>

#include "noncopyable.h"
#include "Timestamp.h"

class Channel;
class EventLoop;

// muduo库中多路事件分发器的核心IO复用模块
class Poller
{
public:
    using ChannelList = std::vector<Channel *>; // 活动通道列表
    // 用 vector 存储活跃 Channel：适合批量遍历和顺序处理，利用其连续内存和缓存友好性。

    Poller(EventLoop *loop); // 构造函数，传入所属的EventLoop
    virtual ~Poller() = default; // 虚析构函数，确保派生类可以正确析构

    // 给所有IO复用保留统一的接口
    // 虚函数在基类中定义，在派生类中实现具体的IO复用逻辑
    virtual Timestamp poll(int timeoutMs, ChannelList *activeChannels) = 0;
    virtual void updateChannel(Channel *channel) = 0;
    virtual void removeChannel(Channel *channel) = 0;

    // 判断参数channel是否在当前的Poller当中
    bool hasChannel(Channel *channel) const; // const 表示这个函数不会修改类的任何成员变量

    // EventLoop可以通过该接口获取默认的IO复用的具体实现
    // 静态工厂方法: 根据当前平台返回一个具体的Poller实现
    // 返回的是基类 Poller*，而不是具体子类（如 EpollPoller*）
    static Poller *newDefaultPoller(EventLoop *loop); 

protected:
    // map的key:sockfd value:sockfd所属的channel通道类型
    // 用 unordered_map 存储所有 Channel：适合根据 sockfd 快速定位，利用其 O (1) 查找效率。
    using ChannelMap = std::unordered_map<int, Channel *>; // sockfd到Channel的映射
    ChannelMap channels_; // Poller中管理的Channel列表

    // ChannelList和 ChannelMap的关系：
    // ChannelList是一个活动通道列表，存储当前Poller中发生事件的Channel指针。
    // ChannelMap是一个哈希表，存储所有注册到Poller中的Channel，key是文件描述符（sockfd），value是对应的Channel指针。
    // Poller的子类需要实现具体的poll方法来处理事件
    // 通过poll方法获取活动通道列表时，会将发生事件的Channel指针存储到activeChannels中。
    // Poller的子类需要实现updateChannel和removeChannel方法来更新或删除Channel。
    // 通过updateChannel方法可以将Channel添加到Poller中，或更新其状态。
    // 通过removeChannel方法可以从Poller中删除Channel。
    // 这样设计使得Poller可以灵活地管理和调度多个Channel的事件处理。


private:
    EventLoop *ownerLoop_; // 定义Poller所属的事件循环EventLoop
    // Poller 是在 EventLoop 的事件循环线程中被创建和使用的。
};