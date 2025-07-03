#include "AsyncTaskQueue.h"

namespace kama {

// 单例模式实现
AsyncTaskQueue& AsyncTaskQueue::getInstance() {
    static AsyncTaskQueue instance;
    return instance;
}

bool AsyncTaskQueue::isRunning() const {
    return !m_workers.empty() && !m_stop;
}

bool AsyncTaskQueue::init(size_t threadCount) {
    // 如果已经有线程在运行，返回失败
    if (!m_workers.empty()) {
        return false;
    }

    m_stop = false;
    
    // 如果未指定线程数量，则使用硬件支持的并发数
    if (threadCount == 0) {
        threadCount = std::thread::hardware_concurrency();
        // 至少使用2个线程
        threadCount = threadCount > 1 ? threadCount : 2;
    }

    // 创建工作线程
    try {
        for (size_t i = 0; i < threadCount; ++i) {
            m_workers.emplace_back(&AsyncTaskQueue::workerThread, this);
        }
        return true;
    }
    catch (...) {
        shutdown();
        return false;
    }
}

void AsyncTaskQueue::workerThread() {
    while (true) {
        Task task;
        
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            
            // 等待条件：有任务或收到停止信号
            m_condition.wait(lock, [this] { 
                return m_stop || !m_tasks.empty(); 
            });
            
            // 如果停止信号且任务队列为空，终止线程
            if (m_stop && m_tasks.empty()) {
                return;
            }
            
            // 获取任务
            task = std::move(m_tasks.front());
            m_tasks.pop();
        }
        
        // 执行任务
        m_activeThreads++;
        task();
        m_activeThreads--;
    }
}

size_t AsyncTaskQueue::getPendingTaskCount() const {
    std::unique_lock<std::mutex> lock(m_queueMutex);
    return m_tasks.size();
}

void AsyncTaskQueue::shutdown() {
    {
        std::unique_lock<std::mutex> lock(m_queueMutex);
        m_stop = true;
    }
    
    // 通知所有等待的线程
    m_condition.notify_all();
    
    // 等待所有工作线程结束
    for (auto& worker : m_workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    
    // 清空线程列表
    m_workers.clear();
}

void AsyncTaskQueue::waitForAll() {
    // 等待所有任务完成
    while (true) {
        {
            std::unique_lock<std::mutex> lock(m_queueMutex);
            if (m_tasks.empty() && m_activeThreads == 0) {
                break;
            }
        }
        
        // 短暂休眠，避免CPU空转
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

AsyncTaskQueue::~AsyncTaskQueue() {
    shutdown();
}

} // namespace kama
