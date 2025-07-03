#include "services/AsyncLlamaService.h"
#include <iostream>
#include <chrono>
#include <future>

namespace kama {
namespace services {

AsyncLlamaService::AsyncLlamaService(unsigned int timeout)
    : m_timeout(timeout) {
}

AsyncLlamaService::~AsyncLlamaService() {
}

std::string AsyncLlamaService::query(const std::string& prompt) {
    // 同步查询，等待异步结果
    auto future = queryAsync(prompt);
    
    if (m_timeout == 0) {
        // 无超时，一直等待
        return future.get();
    } else {
        // 有超时限制
        auto status = future.wait_for(std::chrono::milliseconds(m_timeout));
        
        if (status == std::future_status::ready) {
            return future.get();
        } else {
            return "错误：查询超时";
        }
    }
}

std::future<std::string> AsyncLlamaService::queryAsync(const std::string& prompt) {
    // 提交任务到异步队列
    auto future = AsyncTaskQueue::getInstance().submit(
        [this, prompt]() {
            std::string result = queryInternal(prompt);
            
            // 如果设置了回调函数，调用回调
            if (m_completionCallback) {
                m_completionCallback(prompt, result);
            }
            
            return result;
        }
    );
    
    return future;
}

std::string AsyncLlamaService::queryInternal(const std::string& prompt) {
    // 从模型实例池获取一个可用实例
    auto instancePtr = ModelInstancePool::getInstance().getAvailableInstance();
    
    if (!instancePtr) {
        return "错误：没有可用的模型实例";
    }
    
    try {
        // 执行查询
        std::string result = instancePtr->query(prompt);
        
        // 释放实例
        ModelInstancePool::getInstance().releaseInstance(instancePtr);
        
        return result;
    }
    catch (const std::exception& e) {
        // 报告实例故障
        ModelInstancePool::getInstance().reportInstanceFailure(instancePtr);
        
        std::cerr << "模型查询失败: " << e.what() << std::endl;
        return "错误：模型查询异常 - " + std::string(e.what());
    }
}

void AsyncLlamaService::setCompletionCallback(std::function<void(const std::string&, const std::string&)> callback) {
    m_completionCallback = callback;
}

bool AsyncLlamaService::isAvailable() const {
    std::cout << "AsyncLlamaService::isAvailable - 检查服务可用性" << std::endl;
    
    // 首先检查AsyncTaskQueue是否已初始化
    auto& taskQueue = AsyncTaskQueue::getInstance();
    if (!taskQueue.isRunning()) {
        std::cout << "AsyncLlamaService::isAvailable - 异步任务队列未运行" << std::endl;
        return false;
    }
    
    // 检查ModelInstancePool是否已初始化
    auto& pool = ModelInstancePool::getInstance();
    
    try {
        std::string status = pool.getPoolStatus();
        std::cout << "AsyncLlamaService::isAvailable - 获取实例池状态: " << status << std::endl;
        
        // 如果实例池为空，则不可用
        if (status.find("\"total_instances\": 0") != std::string::npos) {
            std::cout << "AsyncLlamaService::isAvailable - 模型实例池为空" << std::endl;
            return false;
        }
        
        // 尝试获取可用实例
        std::cout << "AsyncLlamaService::isAvailable - 尝试获取可用实例" << std::endl;
        auto instancePtr = pool.getAvailableInstance();
        
        if (!instancePtr) {
            std::cout << "AsyncLlamaService::isAvailable - 无法获取可用实例" << std::endl;
            return false;
        }
        
        // 检查实例是否真正可用
        bool instanceAvailable = false;
        try {
            instanceAvailable = instancePtr->isAvailable();
            std::cout << "AsyncLlamaService::isAvailable - 实例可用性: " << (instanceAvailable ? "可用" : "不可用") << std::endl;
        } 
        catch (const std::exception& e) {
            std::cout << "AsyncLlamaService::isAvailable - 检查实例可用性时发生异常: " << e.what() << std::endl;
            instanceAvailable = false;
        }
        
        // 释放实例
        std::cout << "AsyncLlamaService::isAvailable - 释放实例" << std::endl;
        pool.releaseInstance(instancePtr);
        
        return instanceAvailable;
    }
    catch (const std::exception& e) {
        std::cout << "AsyncLlamaService::isAvailable - 检查过程中发生异常: " << e.what() << std::endl;
        return false;
    }
}

bool AsyncLlamaService::resetConnection() {
    // 对于异步服务，这个操作无意义，因为每次查询都会获取一个实例
    return true;
}

} // namespace services
} // namespace kama
