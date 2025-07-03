#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <thread>
#include <vector>
#include <atomic>
#include <memory>
#include "noncopyable.h"

namespace kama {

/**
 * @brief 异步任务队列
 * 
 * 提供线程池功能，可以异步执行任务，并获取执行结果
 */
class AsyncTaskQueue : noncopyable {
public:
    using Task = std::function<void()>;

    /**
     * @brief 获取单例实例
     */
    static AsyncTaskQueue& getInstance();

    /**
     * @brief 初始化任务队列
     * 
     * @param threadCount 线程数量，默认为系统CPU核心数
     * @return 初始化是否成功
     */
    bool init(size_t threadCount = 0);
    
    /**
     * @brief 检查任务队列是否正在运行
     * 
     * @return 任务队列运行状态
     */
    bool isRunning() const;

    /**
     * @brief 提交任务到队列
     * 
     * @tparam F 函数类型
     * @tparam Args 参数类型包
     * @param f 函数对象
     * @param args 函数参数
     * @return std::future<ReturnType> 任务执行结果的future
     */
    template<typename F, typename... Args>
    auto submit(F&& f, Args&&... args) 
        -> std::future<typename std::result_of<F(Args...)>::type>;

    /**
     * @brief 获取当前等待任务数量
     */
    size_t getPendingTaskCount() const;

    /**
     * @brief 关闭任务队列
     */
    void shutdown();

    /**
     * @brief 等待所有任务完成
     */
    void waitForAll();

    /**
     * @brief 销毁实例
     */
    ~AsyncTaskQueue();

private:
    AsyncTaskQueue() = default;
    
    // 工作线程函数
    void workerThread();

private:
    std::vector<std::thread> m_workers;
    std::queue<Task> m_tasks;
    
    // 同步变量
    mutable std::mutex m_queueMutex;
    std::condition_variable m_condition;
    std::atomic<bool> m_stop{false};
    std::atomic<size_t> m_activeThreads{0};
};

// 模板方法实现
template<typename F, typename... Args>
auto AsyncTaskQueue::submit(F&& f, Args&&... args) 
    -> std::future<typename std::result_of<F(Args...)>::type> {
    
    using ReturnType = typename std::result_of<F(Args...)>::type;

    // 创建可调用任务包，绑定函数和参数
    auto task = std::make_shared<std::packaged_task<ReturnType()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );

    // 获取future对象，用于获取任务结果
    std::future<ReturnType> result = task->get_future();

    {
        std::unique_lock<std::mutex> lock(m_queueMutex);

        // 停止状态不接受新任务
        if(m_stop) {
            throw std::runtime_error("AsyncTaskQueue已关闭，无法提交新任务");
        }

        // 添加任务到队列
        m_tasks.emplace([task]() { (*task)(); });
    }

    // 通知一个等待线程有新任务
    m_condition.notify_one();
    return result;
}

} // namespace kama
