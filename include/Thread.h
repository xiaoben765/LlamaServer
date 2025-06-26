#pragma once

#include <functional>
#include <thread>
#include <memory>
#include <unistd.h>
#include <string>
#include <atomic>

#include "noncopyable.h"

// 4. Thread 类的声明
class Thread : noncopyable // 继承自 noncopyable
{
public: // 公共接口，类的使用者可以直接调用
    // 5. 定义一个类型别名 ThreadFunc
    using ThreadFunc = std::function<void()>;

    // 6. 构造函数和析构函数
    explicit Thread(ThreadFunc, const std::string &name = std::string());
    ~Thread();

    // 7. 核心成员函数
    void start();
    void join();

    // 8. 获取线程信息的函数
    bool started() { return started_; } // 返回线程是否已启动
    pid_t tid() const { return tid_; } // 返回线程的ID
    const std::string &name() const { return name_; } // 返回线程的名字

    // 9. 静态成员函数
    static int numCreated() { return numCreated_; } // 返回已创建的线程数量

private: // 私有成员，只有类的内部可以访问
    void setDefaultName(); // 内部辅助函数

    // 10. 成员变量
    bool started_;
    bool joined_; // 线程是否已被 join
    std::shared_ptr<std::thread> thread_; // 指向线程对象的智能指针
    pid_t tid_;                           // 线程ID
    ThreadFunc func_;                     // 线程要执行的函数
    std::string name_;                    // 线程的名字
    static std::atomic_int numCreated_;   // 静态原子变量，记录已创建的线程数量
};