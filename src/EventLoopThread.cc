#include <EventLoopThread.h>
#include <EventLoop.h>

// EventLoopThread类的构造函数
EventLoopThread::EventLoopThread(const ThreadInitCallback &cb,
                                 const std::string &name)
    : loop_(nullptr) // loop_指向EventLoop对象的指针
    , exiting_(false) // exiting_标识线程是否正在退出
    , thread_(std::bind(&EventLoopThread::threadFunc, this), name) // thread_是Thread类的对象，绑定了线程函数threadFunc
    , mutex_() // 互斥锁mutex_ 用于保护共享资源的线程安全访问
    , cond_() // 条件变量cond_ 用于线程间的通信
    , callback_(cb) // callback_是线程初始化回调函数，当线程启动时会调用这个函数来进行一些初始化操作
{
}

EventLoopThread::~EventLoopThread()
{
    exiting_ = true;
    if (loop_ != nullptr) // 创建了 EventLoopThread 对象但从未调用 startLoop()，那么 loop_ 将一直是 nullptr
    {
        loop_->quit(); // 如果 loop_ 有效，就调用 EventLoop 的 quit() 方法。这个方法的作用是通知 EventLoop 对象停止其事件循环。
        // join() 会阻塞当前线程（即调用析构函数的线程，通常是主线程），直到 thread_ 所代表的IO线程完全执行完毕。
        thread_.join(); // 等待线程结束，确保线程资源被正确释放
    }
}

// startLoop() 方法的作用是启动一个新的线程，并在该线程中创建一个 EventLoop 对象并开始运行事件循环。
// startLoop() 是“前台接待员”，而 threadFunc() 是“后台工程师”。
// 顺序可以理解为：通过 startLoop() 方法，主线程（通常是主循环）会创建一个新的线程，并在这个新线程中执行 threadFunc() 方法。
EventLoop *EventLoopThread::startLoop()
{
    thread_.start(); // 调用 Thread 对象的 start() 方法，这会真正地创建并启动操作系统内核线程。
    // 调用后面的 threadFunc() 方法，这个方法会在新创建的线程中执行。

    EventLoop *loop = nullptr; // loop_是一个指向EventLoop对象的指针，初始时为nullptr
    {
        std::unique_lock<std::mutex> lock(mutex_);
        // 主线程main会阻塞在这里，直到工作线程被创建
        cond_.wait(lock, [this](){return loop_ != nullptr;});  // 在等待时，它会自动解开 lock。直到 loop_ 被初始化为非 nullptr 时，才会继续执行后面的代码。
        // [this](){return loop_ != nullptr;} 是一个 lambda 表达式，用于在条件变量上等待，直到 loop_ 被初始化。
        loop = loop_; // 将初始化好的 EventLoop 对象的指针赋值给 loop
    }
    return loop;
}

// 在新创建的IO线程中执行的代码
void EventLoopThread::threadFunc()
{
    EventLoop loop; // 创建一个独立的EventLoop对象 和上面的线程是一一对应的 级one loop per thread

    if (callback_) // 如果有线程初始化回调函数
    {
        // 在 EventLoop 刚刚创建好、但还没有开始 loop() 循环之前，做一些线程专属的初始化工作。
        callback_(&loop); // 将新创建的 loop 对象的指针传给它
        // 为什么要传递 EventLoop 指针？因为回调函数可能需要在新线程中对 EventLoop 进行一些初始化操作，比如设置定时器、注册事件等。
        // 下面这些操作会在创建的EventLoopThread 对象中，传入定义好的回调函数
        /* 比如：
            一个网络服务器，可能希望在这个IO线程里初始化一个客户端连接的计数器。
            一个日志系统，可能希望为这个新线程设置一个专属的、线程安全的日志记录器。
            一个后台任务处理器，可能希望在这个工作线程里预先加载一些数据到内存。
        */
    }

    {
        std::unique_lock<std::mutex> lock(mutex_);
        loop_ = &loop;
        cond_.notify_one(); // 通知唤醒那个正在 cond_.wait() 上等待的线程（即主线程）。
    }
    loop.loop();    // 进入事件循环。这是IO线程生命周期中最长的一个阶段。这个函数会阻塞，在内部不断地等待和处理IO事件
    std::unique_lock<std::mutex> lock(mutex_);
    loop_ = nullptr; // 在栈上的 loop 对象销毁前，我们再次加锁，将共享的 loop_ 指针设为 nullptr
}