#pragma once // 防止头文件被多次包含

#include <functional> // std::function
#include <memory> // std::shared_ptr

#include "noncopyable.h" // noncopyable.h 用于禁止拷贝构造和赋值操作
#include "Timestamp.h" // Timestamp.h 用于时间戳的处理

class EventLoop;

/**
 * 理清楚 EventLoop、Channel、Poller之间的关系  Reactor模型上对应多路事件分发器
 * Channel理解为通道 封装了sockfd和其感兴趣的event 如EPOLLIN、EPOLLOUT事件 还绑定了poller返回的具体事件
 **/
class Channel : noncopyable
{
public:
    using EventCallback = std::function<void()>; // 事件回调函数，无参数
    using ReadEventCallback = std::function<void(Timestamp)>; // 读事件回调函数，接收一个时间戳参数

    Channel(EventLoop *loop, int fd); // 构造函数，输入参数表示Channel所属的EventLoop和文件描述符fd
    ~Channel();

    // fd得到Poller通知以后 处理事件 handleEvent在EventLoop::loop()中调用
    void handleEvent(Timestamp receiveTime);

    // 设置回调函数对象
    void setReadCallback(ReadEventCallback cb) { readCallback_ = std::move(cb); } // 设置读事件回调函数
    void setWriteCallback(EventCallback cb) { writeCallback_ = std::move(cb); } //  设置写事件回调函数
    void setCloseCallback(EventCallback cb) { closeCallback_ = std::move(cb); } // 设置关闭事件回调函数
    void setErrorCallback(EventCallback cb) { errorCallback_ = std::move(cb); } // 设置错误事件回调函数

    // 防止当channel被手动remove掉 channel还在执行回调操作
    void tie(const std::shared_ptr<void> &); // 绑定一个shared_ptr对象到Channel上
    // 通过shared_ptr的weak_ptr来判断Channel是否被销毁

    int fd() const { return fd_; } // 获取文件描述符
    int events() const { return events_; } // 获取注册的事件, 即Poller感兴趣的事件
    void set_revents(int revt) { revents_ = revt; } // 设置Poller返回的具体事件, 即发生的事件

    // 设置fd相应的事件状态 相当于epoll_ctl add delete
    void enableReading() { events_ |= kReadEvent; update(); } // 启用读事件
    void disableReading() { events_ &= ~kReadEvent; update(); } // 禁用读事件
    void enableWriting() { events_ |= kWriteEvent; update(); } // 启用写事件
    void disableWriting() { events_ &= ~kWriteEvent; update(); } // 禁用写事件
    void disableAll() { events_ = kNoneEvent; update(); } // 禁用所有事件

    // 返回fd当前的事件状态
    bool isNoneEvent() const { return events_ == kNoneEvent; } // 是否没有事件
    bool isWriting() const { return events_ & kWriteEvent; } // 是否有写事件
    bool isReading() const { return events_ & kReadEvent; } // 是否有读事件

    int index() { return index_; } // 获取在Poller中的索引位置, 即在Poller的Channel列表中的位置, 数据结构是哈希表<sockfd, Channel*>
    void set_index(int idx) { index_ = idx; } // 设置channel在Poller中的索引位置
    // set_index()方法用于设置Channel在Poller中的索引位置，通常用于标识Channel的状态，如新建、已添加、已删除等

    // one loop per thread
    // "one loop per thread"模式: 每个Channel对象只属于一个EventLoop
    // Channel对象只能在其所属的EventLoop线程中操作，保证线程安全
    // ownerLoop()方法返回此Channel所属的EventLoop指针，用于线程安全检查
    // 这种设计符合Reactor模式，避免了多线程并发访问同一Channel的竞争问题

    /* 下面的例子好理解些：
        客户端连接（Socket）对应一个 Channel，被分配到某个 EventLoop 线程。
        EventLoop 监听 Channel 的读事件（如 HTTP 请求到达），触发回调函数处理请求（类似应用层逻辑）。
        处理完成后，通过同一 EventLoop 线程发送响应，确保操作在单线程内完成。
    */
    EventLoop *ownerLoop() { return loop_; } // 获取Channel所属的EventLoop， 非 const 对象，允许修改返回的EventLoop指针（如调用其非 const 方法）
    const EventLoop *ownerLoop() const { return loop_; } // 函数重载，用于const 对象，保证不修改Channel内部状态，且返回的EventLoop指针也不可修改。
    void remove();
private:

    void update(); // 更新Channel在Poller中的状态，调用EventLoop的updateChannel方法
    void handleEventWithGuard(Timestamp receiveTime); // 处理事件时，先检查是否绑定了shared_ptr对象，如果绑定了则使用它来保证Channel的生命周期

    static const int kNoneEvent;
    static const int kReadEvent;
    static const int kWriteEvent;

    EventLoop *loop_; // 事件循环
    // 文件描述符是非负整数，通常用于表示打开的文件、网络连接等资源。
    const int fd_;    // fd，Poller监听的对象

    // 位掩码的设计，使用整数的位来表示不同的事件类型
    int events_;      // 注册fd感兴趣的事件
    int revents_;     // Poller返回的具体发生的事件

    // index_用于标识Channel在Poller中的位置，-1表示未注册
    int index_;

    std::weak_ptr<void> tie_; // 用于绑定一个shared_ptr对象到Channel上，防止Channel在回调中被销毁
    // weak_ptr是一个智能指针，它不拥有对象的所有权，只是观察对象的生命周期
    // 当Channel的回调函数被调用时，先检查tie_是否有效，如果有效则执行回调，否则不执行
    bool tied_; // 是否绑定了shared_ptr对象

    // 因为channel通道里可获知fd最终发生的具体的事件events，所以它负责调用具体事件的回调操作
    ReadEventCallback readCallback_;
    EventCallback writeCallback_;
    EventCallback closeCallback_;
    EventCallback errorCallback_;
};