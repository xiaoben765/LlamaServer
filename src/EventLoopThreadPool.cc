#include <memory>

#include <EventLoopThreadPool.h>
#include <EventLoopThread.h>
#include <Logger.h>
EventLoopThreadPool::EventLoopThreadPool(EventLoop *baseLoop, const std::string &nameArg)
    : baseLoop_(baseLoop), name_(nameArg), started_(false), numThreads_(0), next_(0), hash_(3) // 初始化一致性哈希，虚拟节点数为3
{
}

EventLoopThreadPool::~EventLoopThreadPool()
{
    // Don't delete loop, it's stack variable
    // 这个析构函数是空的，因为所有动态分配的资源 (EventLoopThread)
    // 都由 std::unique_ptr 自动管理了。当 EventLoopThreadPool 对象销毁时，
    // 其成员变量 threads_ (vector) 会被销毁，
    // vector 中的每个 unique_ptr 也会被销毁，
    // 最终导致它们指向的 EventLoopThread 对象被自动 delete。
}

void EventLoopThreadPool::start(const ThreadInitCallback &cb)
{
    started_ = true; // 标记为已启动

    // 循环创建指定数量的线程
    for (int i = 0; i < numThreads_; ++i)
    {
        // 1. 为新线程创建一个名字，例如 "poolName-0", "poolName-1"
        char buf[name_.size() + 32];
        snprintf(buf, sizeof buf, "%s%d", name_.c_str(), i);

        // 2. 创建 EventLoopThread 对象
        EventLoopThread *t = new EventLoopThread(cb, buf);

        // 3. 将其所有权交给 std::unique_ptr，并存入 threads_ 容器
        threads_.push_back(std::unique_ptr<EventLoopThread>(t));

        // 4. 启动新线程，并在该线程中创建 EventLoop，然后返回其指针
        //    这是最关键的一步！
        loops_.push_back(t->startLoop()); // 当创建了一个 EventLoop 对象后，然后返回这个 EventLoop 的地址，并开始事件循环 loop()。返回的地址被存入了 loops_ 向量，方便之后快速获取。

        // 5. 将新创建的线程节点加入到一致性哈希环中
        hash_.addNode(buf);
    }

    // 特殊情况：如果线程数为0，那么所有任务都在 baseLoop_ 中运行
    if (numThreads_ == 0) 
    {
        // 即使没有子线程，也要向哈希环中添加一个节点，避免getNode方法抛出异常
        char buf[name_.size() + 32];
        snprintf(buf, sizeof buf, "%s%s", name_.c_str(), "_base");
        
        // 确保清除哈希环中的所有节点并添加新节点，防止潜在的历史残留问题
        LOG_INFO << "初始化一致性哈希环，添加基础节点: " << buf;
        
        // 添加基础节点到哈希环
        hash_.addNode(buf);
        
        LOG_INFO << "EventLoopThreadPool使用单线程模式，所有连接将由baseLoop处理";
        
        // 如果用户提供了初始化回调，就在 baseLoop_ 上执行它
        if (cb) {
            LOG_INFO << "执行线程初始化回调";
            cb(baseLoop_);
        }
    }
}

// 如果工作在多线程中，baseLoop_(mainLoop)会默认以轮询的方式分配Channel给subLoop
// 一致性哈希，对于同一个 key，它总是能映射到同一个节点（线程）。
// 在网络编程中，通常使用客户端的 "IP地址+端口号" 作为 key。
EventLoop* EventLoopThreadPool::getNextLoop(const std::string &key)
{
    // 如果没有子线程，直接返回baseLoop_，但仍然尝试使用哈希算法
    // 为numThreads_=0的情况增加了额外的日志，帮助诊断问题
    if (loops_.empty())
    {
        LOG_INFO << "EventLoopThreadPool::getNextLoop - 线程池为空，使用baseLoop_处理请求，key=" << key;
        
        // 仍然需要使用一致性哈希，因为start方法中已经添加了基础节点（即使线程数为0）
        // 注意：这里不直接返回，让程序继续执行哈希逻辑
    }
    
    try {
        // 如果线程池为空，直接返回baseLoop_
        if (loops_.empty()) {
            LOG_INFO << "线程池为空，直接使用baseLoop_处理请求";
            return baseLoop_;
        }
        
        // 1. 通过一致性哈希算法，根据传入的 key 计算出应该由哪个节点（线程）处理
        size_t hashValue = hash_.getNode(key);
        
        // 再次检查loops_是否为空，避免因为潜在的并发问题导致除以0错误
        if (loops_.empty())
        {
            LOG_INFO << "计算哈希后发现线程池为空，使用baseLoop_";
            return baseLoop_;
        }
        
        // 计算在loops_中的索引
        size_t index = hashValue % loops_.size();
        
        // 2. 如果计算出的索引超出了 loops_ 的范围，
        if (index >= loops_.size())
        {
            // 如果索引无效（理论上不应发生，除非哈希实现有问题），
            // 记录错误日志并返回主循环作为备用。
            LOG_ERROR << "EventLoopThreadPool::getNextLoop ERROR - index out of range";
            return baseLoop_;
        }
        
        // 3. 返回选中的 EventLoop 的指针
        LOG_INFO << "使用一致性哈希选择线程，索引=" << index;
        return loops_[index];
    }
    catch (const std::exception& e) {
        // 如果一致性哈希抛出异常（例如，没有节点），记录错误并返回baseLoop_
        LOG_ERROR << "EventLoopThreadPool::getNextLoop 一致性哈希异常: " << e.what() 
                 << " [key=" << key << ", numThreads=" << numThreads_ << "]";
                 
        // 这里可能是因为哈希环中没有足够的节点，需要确保start()方法中已正确处理numThreads_=0的情况
        return baseLoop_;
    }
}

std::vector<EventLoop *> EventLoopThreadPool::getAllLoops()
{
    if (loops_.empty()) // 如果线程池中没有线程
    {
        // 返回一个只包含 baseLoop_ 的 vector
        return std::vector<EventLoop *>(1, baseLoop_);
    }
    else
    {
        // 否则，返回所有子线程的 EventLoop 列表
        return loops_;
    }
}