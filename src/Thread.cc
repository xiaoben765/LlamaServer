#include <Thread.h>
#include <CurrentThread.h>

#include <semaphore.h>

std::atomic_int Thread::numCreated_(0);

// 构造函数
Thread::Thread(ThreadFunc func, const std::string &name)
    : started_(false),       // 初始化 started_ 为 false
      joined_(false),        // 初始化 joined_ 为 false
      tid_(0),               // 初始化 tid_ 为 0
      func_(std::move(func)),// 使用 std::move 高效地转移 func 的所有权，避免不必要的拷贝
      name_(name)            // 拷贝 name
{
    setDefaultName(); // 设置一个默认名字
}

Thread::~Thread()
{
    // 如果线程已经启动，并且没有被join过
    // 使用detach()方法分离线程，这样分离后的线程在执行完毕后会自动释放资源
    if (started_ && !joined_)
    {
        thread_->detach();                                                  // thread类提供了设置分离线程的方法 线程运行后自动销毁（非阻塞）
    }
}

// 核心：启动线程
void Thread::start()
{
    started_ = true;
    sem_t sem;
    sem_init(&sem, false, 0); // 1. 初始化一个信号量，初始值为0

    // 2. 创建真正的 std::thread 对象
    thread_ = std::shared_ptr<std::thread>(new std::thread([&]() {
        // --- 这部分代码是在新线程中执行的 ---
        tid_ = CurrentThread::tid(); // 3. 获取新线程的ID
        sem_post(&sem);             // 4. 信号量+1，通知主线程ID已获取
        func_();                    // 5. 执行用户传入的真正任务
        // --- 新线程执行结束 ---
    }));

    // 6. 主线程在这里等待，直到信号量的值 > 0
    sem_wait(&sem);
}

// C++ std::thread 中join()和detach()的区别：https://blog.nowcoder.net/n/8fcd9bb6e2e94d9596cf0a45c8e5858a
void Thread::join()
{
    joined_ = true;
    thread_->join();
}

void Thread::setDefaultName()
{
    int num = ++numCreated_;
    if (name_.empty())
    {
        char buf[32] = {0};
        snprintf(buf, sizeof buf, "Thread%d", num);
        name_ = buf;
    }
}
