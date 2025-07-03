#pragma once

#include "services/ILlamaService.h"
#include "noncopyable.h"
#include <string>
#include <memory>
#include <list>
#include <vector>
#include <mutex>
#include <atomic>
#include <unordered_map>
#include <chrono>
#include <thread>

namespace kama {
namespace services {

/**
 * @brief 模型实例池
 * 
 * 管理多个LLaMA服务实例，支持负载均衡和故障转移
 */
class ModelInstancePool : noncopyable {
public:
    /**
     * @brief 获取单例实例
     */
    static ModelInstancePool& getInstance();
    
    /**
     * @brief 初始化模型实例池
     * 
     * @param modelPath 模型路径
     * @param hosts 服务器地址列表 (格式: "ip:port")
     * @param instancesPerHost 每个服务器上的实例数量
     * @return 初始化是否成功
     */
    bool init(const std::string& modelPath, 
              const std::vector<std::string>& hosts,
              int instancesPerHost = 1);

    /**
     * @brief 使用模拟服务初始化模型实例池 (用于测试)
     * 
     * @param instanceCount 模拟实例数量
     * @param delayMs 模拟处理延迟(毫秒)
     * @param failRate 模拟失败率(0-100)
     * @return 初始化是否成功
     */
    bool initWithMockServices(int instanceCount = 5, int delayMs = 100, int failRate = 10);
              
    /**
     * @brief 获取可用的LLaMA服务实例
     * 
     * @return 服务实例指针
     */
    std::shared_ptr<ILlamaService> getAvailableInstance();
    
    /**
     * @brief 释放实例（标记为可用）
     * 
     * @param instance 服务实例
     */
    void releaseInstance(std::shared_ptr<ILlamaService> instance);
    
    /**
     * @brief 报告实例故障
     * 
     * @param instance 故障服务实例
     */
    void reportInstanceFailure(std::shared_ptr<ILlamaService> instance);
    
    /**
     * @brief 获取池状态信息
     * 
     * @return 池状态JSON字符串
     */
    std::string getPoolStatus() const;
    
    /**
     * @brief 关闭实例池
     */
    void shutdown();
    
private:
    // 模型实例信息
    class InstanceInfo {
    public:
        std::shared_ptr<ILlamaService> instance;
        bool inUse;
        std::chrono::steady_clock::time_point lastUseTime;
        int failureCount;
        std::string host;
        int port;
        
        // 基本构造函数
        InstanceInfo() : instance(nullptr), inUse(false), failureCount(0), port(0) {}
        
        // 允许移动操作，但删除复制操作
        InstanceInfo(const InstanceInfo&) = delete;
        InstanceInfo& operator=(const InstanceInfo&) = delete;
        
        // 自定义移动构造函数
        InstanceInfo(InstanceInfo&& other) noexcept
            : instance(std::move(other.instance)),
              inUse(other.inUse),
              lastUseTime(other.lastUseTime),
              failureCount(other.failureCount),
              host(std::move(other.host)),
              port(other.port) {
            other.inUse = false;
            other.failureCount = 0;
            other.port = 0;
        }
        
        // 自定义移动赋值运算符
        InstanceInfo& operator=(InstanceInfo&& other) noexcept {
            if (this != &other) {
                instance = std::move(other.instance);
                inUse = other.inUse;
                lastUseTime = other.lastUseTime;
                failureCount = other.failureCount;
                host = std::move(other.host);
                port = other.port;
                
                other.inUse = false;
                other.failureCount = 0;
                other.port = 0;
            }
            return *this;
        }
        
        // 获取故障计数
        int getFailureCount() const { return failureCount; }
    };
    
    ModelInstancePool() = default;
    
    // 健康检查线程
    void healthCheckThread();
    
    // 尝试恢复故障实例
    void tryRecoverFailedInstances();
    
private:
    std::string m_modelPath;
    std::list<InstanceInfo> m_instances;
    mutable std::mutex m_poolMutex;
    std::atomic<bool> m_running{false};
    std::thread m_healthCheckThread;
    
    // 负载均衡计数器
    std::atomic<size_t> m_roundRobinCounter{0};
};

} // namespace services
} // namespace kama
