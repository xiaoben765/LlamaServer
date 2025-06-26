#pragma once

#include <functional>
#include <mutex> // 当多个线程需要访问共享资源时，为了防止数据竞争和不一致，需要用互斥锁来保护。一次只有一个线程能获得锁。
#include <condition_variable> // 条件变量用于线程间的通信，它允许一个或多个线程等待某个特定条件成立。它总是和 std::mutex 一起使用。
#include <string>

#include "noncopyable.h"
#include "Thread.h"

class EventLoop;

class EventLoopThread : noncopyable
{
public:
    using ThreadInitCallback = std::function<void(EventLoop *)>;

    EventLoopThread(const ThreadInitCallback &cb = ThreadInitCallback(),
                    const std::string &name = std::string());
    ~EventLoopThread();

    EventLoop *startLoop(); // 启动线程，并在该线程中创建一个 EventLoop 对象并开始运行事件循环

private:
    void threadFunc(); // 线程函数，当调用 startLoop 创建新线程时，这个 threadFunc 函数就会在新线程中被执行。

    EventLoop *loop_; // 指向新线程中创建的 EventLoop 对象的指针
    bool exiting_; // 标识线程是否正在退出
    Thread thread_; // 用于创建和管理线程的 Thread 对象
    std::mutex mutex_;             // 互斥锁
    std::condition_variable cond_; // 条件变量
    ThreadInitCallback callback_; // 线程初始化回调函数，当线程启动时会调用这个函数来进行一些初始化操作
};