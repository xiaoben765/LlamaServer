#include "services/ModelInstancePool.h"
#include "services/LlamaService.h"
#include "services/LlamaMockService.h"
#include <nlohmann/json.hpp>
#include <sstream>
#include <chrono>
#include <thread>
#include <iostream>

namespace kama {
namespace services {

using json = nlohmann::json;

// 单例实现
ModelInstancePool& ModelInstancePool::getInstance() {
    static ModelInstancePool instance;
    return instance;
}

bool ModelInstancePool::init(const std::string& modelPath,
                           const std::vector<std::string>& hosts,
                           int instancesPerHost) {
    std::lock_guard<std::mutex> lock(m_poolMutex);
    
    if (m_running || !m_instances.empty()) {
        return false; // 已经初始化
    }
    
    m_modelPath = modelPath;
    m_running = true;
    
    // 为每个主机创建多个实例
    for (const auto& hostInfo : hosts) {
        // 解析主机地址和端口
        std::string host = hostInfo;
        int port = 8899; // 默认端口
        
        size_t colonPos = hostInfo.find(':');
        if (colonPos != std::string::npos) {
            host = hostInfo.substr(0, colonPos);
            port = std::stoi(hostInfo.substr(colonPos + 1));
        }
        
        // 为每个主机创建指定数量的实例
        for (int i = 0; i < instancesPerHost; ++i) {
            try {
                auto instance = std::make_shared<LlamaTcpService>(modelPath, host, port);
                
                // 检查实例是否可用
                if (instance->isAvailable()) {
                    // 创建一个新的InstanceInfo对象
                    m_instances.push_back(InstanceInfo());
                    auto& info = m_instances.back();
                    info.instance = instance;
                    info.inUse = false;
                    info.lastUseTime = std::chrono::steady_clock::now();
                    info.host = host;
                    info.port = port;
                    std::cout << "已创建并添加模型实例 - 主机: " << host << ":" << port << std::endl;
                }
            }
            catch (const std::exception& e) {
                std::cerr << "创建模型实例失败: " << e.what() << std::endl;
                // 继续尝试创建其他实例
            }
        }
    }
    
    if (m_instances.empty()) {
        m_running = false;
        return false; // 没有成功创建任何实例
    }
    
    // 启动健康检查线程
    m_healthCheckThread = std::thread(&ModelInstancePool::healthCheckThread, this);
    
    return true;
}

bool ModelInstancePool::initWithMockServices(int instanceCount, int delayMs, int failRate) {
    std::cout << "======== 开始初始化模型实例池（使用模拟服务）... ========" << std::endl;
    
    // 先检查当前状态
    {
        std::lock_guard<std::mutex> checkLock(m_poolMutex);
        std::cout << "初始化前状态检查: running=" << m_running << ", 实例数=" << m_instances.size() << std::endl;
        
        if (m_running && !m_instances.empty()) {
            std::cout << "模型实例池已经初始化且运行中，无法重新初始化" << std::endl;
            return false; // 已经初始化且运行中
        }
        
        if (m_running) {
            std::cout << "警告: 模型实例池标记为运行中但实例列表为空，可能存在状态不一致" << std::endl;
        }
        
        if (!m_instances.empty()) {
            std::cout << "警告: 模型实例池有实例但未标记为运行中，尝试清理实例..." << std::endl;
            m_instances.clear();
        }
    }
    
    // 获取锁并执行初始化
    std::lock_guard<std::mutex> lock(m_poolMutex);
    
    // 再次检查状态（可能在获取锁的过程中被改变）
    if (m_running || !m_instances.empty()) {
        std::cout << "获取锁后状态已改变，无法初始化" << std::endl;
        return false;
    }
    
    m_modelPath = "mock_model_path";
    m_running = true;
    
    std::cout << "初始化模型实例池（使用模拟服务）：" << instanceCount << " 个实例" 
              << ", 延迟: " << delayMs << "ms"
              << ", 失败率: " << failRate << "%" << std::endl;
    
    int successCount = 0;
    int failedCount = 0;
    
    // 创建指定数量的模拟实例
    for (int i = 0; i < instanceCount; ++i) {
        try {
            std::cout << "创建模拟服务实例 #" << i << "..." << std::endl;
            
            // 直接创建 LlamaMockService
            std::shared_ptr<LlamaMockService> mockService = std::make_shared<LlamaMockService>(delayMs, failRate);
            if (!mockService) {
                std::cerr << "错误: 创建模拟服务失败，shared_ptr为空" << std::endl;
                failedCount++;
                continue;
            }
            
            std::cout << "LlamaMockService 实例创建完成，检查其可用性..." << std::endl;
            
            // 确保模拟服务实例设置为可用
            mockService->setAvailable(true);
            
            // 检查实例是否可用
            bool isAvailable = mockService->isAvailable();
            std::cout << "模拟服务实例 #" << i << " 可用性检查: " << (isAvailable ? "可用" : "不可用") << std::endl;
            
            if (isAvailable) {
                try {
                    // 创建一个新的InstanceInfo对象并明确初始化
                    InstanceInfo newInfo;
                    newInfo.instance = mockService;
                    newInfo.inUse = false;
                    newInfo.lastUseTime = std::chrono::steady_clock::now();
                    newInfo.failureCount = 0;
                    newInfo.host = "mock_host";
                    newInfo.port = i + 9000;  // 模拟端口号
                    
                    // 添加到实例列表
                    m_instances.push_back(std::move(newInfo));
                    
                    std::cout << "已创建并添加模拟模型实例 #" << i << std::endl;
                    successCount++;
                }
                catch (const std::exception& e) {
                    std::cerr << "添加实例到列表时异常: " << e.what() << std::endl;
                    failedCount++;
                }
            } else {
                std::cerr << "模拟服务实例 #" << i << " 不可用，跳过" << std::endl;
                failedCount++;
            }
        }
        catch (const std::exception& e) {
            std::cerr << "创建模拟模型实例失败: " << e.what() << std::endl;
            failedCount++;
        }
    }
    
    std::cout << "模拟服务创建结果: 成功=" << successCount << ", 失败=" << failedCount << std::endl;
    
    if (m_instances.empty()) {
        std::cout << "没有成功创建任何模拟服务实例，初始化失败" << std::endl;
        m_running = false;
        return false; // 没有成功创建任何实例
    }
    
    // 启动健康检查线程
    std::cout << "启动健康检查线程..." << std::endl;
    try {
        m_healthCheckThread = std::thread(&ModelInstancePool::healthCheckThread, this);
        std::cout << "健康检查线程启动成功" << std::endl;
    }
    catch (const std::exception& e) {
        std::cerr << "启动健康检查线程失败: " << e.what() << std::endl;
        // 即使线程启动失败，我们也可以继续使用实例池
    }
    
    std::cout << "模型实例池初始化完成，当前实例数: " << m_instances.size() << std::endl;
    return !m_instances.empty(); // 只要有实例就认为初始化成功
}

std::shared_ptr<ILlamaService> ModelInstancePool::getAvailableInstance() {
    std::lock_guard<std::mutex> lock(m_poolMutex);
    
    std::cout << "getAvailableInstance - 检查实例池状态: 运行中=" << (m_running ? "是" : "否") 
              << ", 实例数=" << m_instances.size() << std::endl;
    
    if (m_instances.empty()) {
        std::cout << "警告: 实例池为空，无法获取实例" << std::endl;
        return nullptr;
    }
    
    // 轮询选择实例 (Round Robin)
    size_t instanceCount = m_instances.size();
    size_t startIdx = m_roundRobinCounter++ % instanceCount;
    
    std::cout << "开始查找可用实例，起始索引: " << startIdx << ", 总实例数: " << instanceCount << std::endl;
    
    // 从list中查找一个可用的实例
    auto it = m_instances.begin();
    // 先向前移动startIdx个位置
    std::advance(it, startIdx);
    
    // 循环查找可用实例
    for (size_t i = 0; i < instanceCount; ++i) {
        std::cout << "检查实例 #" << i << " - 主机: " << it->host << ":" << it->port 
                  << ", 使用中: " << (it->inUse ? "是" : "否") 
                  << ", 故障计数: " << it->failureCount << std::endl;
        
        if (!it->inUse && it->failureCount < 3) {
            if (it->instance) {
                try {
                    // 再次确认实例可用性
                    bool available = it->instance->isAvailable();
                    if (!available) {
                        std::cout << "警告: 实例报告不可用，跳过" << std::endl;
                        ++it;
                        if (it == m_instances.end()) {
                            it = m_instances.begin();
                        }
                        continue;
                    }
                    
                    std::cout << "找到可用实例，标记为使用中" << std::endl;
                    it->inUse = true;
                    it->lastUseTime = std::chrono::steady_clock::now();
                    return it->instance;
                }
                catch (const std::exception& e) {
                    std::cerr << "检查实例可用性时发生异常: " << e.what() << std::endl;
                }
            }
            else {
                std::cout << "警告: 实例指针为空" << std::endl;
            }
        }
        
        // 移动到下一个，如果到达末尾则回到开始
        ++it;
        if (it == m_instances.end()) {
            it = m_instances.begin();
        }
    }
    
    std::cout << "未找到可用实例，所有实例都在使用中或故障中" << std::endl;
    // 如果所有实例都在使用中，返回nullptr
    return nullptr;
}

void ModelInstancePool::releaseInstance(std::shared_ptr<ILlamaService> instance) {
    std::lock_guard<std::mutex> lock(m_poolMutex);
    
    for (auto& info : m_instances) {
        if (info.instance == instance) {
            info.inUse = false;
            info.lastUseTime = std::chrono::steady_clock::now();
            break;
        }
    }
}

void ModelInstancePool::reportInstanceFailure(std::shared_ptr<ILlamaService> instance) {
    std::lock_guard<std::mutex> lock(m_poolMutex);
    
    for (auto& info : m_instances) {
        if (info.instance == instance) {
            info.failureCount++;
            info.inUse = false;
            
            std::cerr << "模型实例故障报告 - 主机: " << info.host << ":" << info.port
                     << ", 故障计数: " << info.failureCount << std::endl;
            
            // 如果故障次数过多，尝试重置连接
            if (info.failureCount >= 3 && info.failureCount < 6) {
                std::cout << "尝试重置模型实例连接 - 主机: " << info.host << ":" << info.port << std::endl;
                if (info.instance->resetConnection()) {
                    info.failureCount = 0;
                    std::cout << "模型实例连接重置成功" << std::endl;
                }
            }
            
            break;
        }
    }
}

void ModelInstancePool::healthCheckThread() {
    while (m_running) {
        // 每10秒检查一次
        std::this_thread::sleep_for(std::chrono::seconds(10));
        
        tryRecoverFailedInstances();
    }
}

void ModelInstancePool::tryRecoverFailedInstances() {
    std::lock_guard<std::mutex> lock(m_poolMutex);
    
    for (auto& info : m_instances) {
        // 尝试恢复故障实例
        if (info.failureCount >= 3 && !info.inUse) {
            std::cout << "尝试恢复故障模型实例 - 主机: " << info.host << ":" << info.port << std::endl;
            
            if (info.instance->resetConnection() && info.instance->isAvailable()) {
                info.failureCount = 0;
                std::cout << "模型实例恢复成功" << std::endl;
            }
            else {
                // 如果恢复失败，但已经尝试了多次，尝试重新创建实例
                if (info.failureCount >= 5) {
                    try {
                        std::cout << "重新创建模型实例 - 主机: " << info.host << ":" << info.port << std::endl;
                        
                        // 检查是否是模拟服务实例（通过主机名判断）
                        if (info.host == "mock_host") {
                            // 这是一个模拟实例，直接创建
                            auto newInstance = std::make_shared<LlamaMockService>(100, 10); // 使用默认延迟和失败率
                            if (newInstance->isAvailable()) {
                                info.instance = newInstance;
                                info.failureCount = 0;
                                std::cout << "模拟模型实例重新创建成功" << std::endl;
                            }
                        } else {
                            // 这是一个真实服务实例
                            auto newInstance = std::make_shared<LlamaTcpService>(m_modelPath, info.host, info.port);
                            if (newInstance->isAvailable()) {
                                info.instance = newInstance;
                                info.failureCount = 0;
                                std::cout << "真实模型实例重新创建成功" << std::endl;
                            }
                        }
                    }
                    catch (const std::exception& e) {
                        std::cerr << "重新创建模型实例失败: " << e.what() << std::endl;
                    }
                }
            }
        }
    }
}

std::string ModelInstancePool::getPoolStatus() const {
    std::lock_guard<std::mutex> lock(m_poolMutex);
    
    std::cout << "调用getPoolStatus() - 实例池大小: " << m_instances.size() << ", 运行状态: " << (m_running ? "运行中" : "未运行") << std::endl;
    
    json status;
    status["total_instances"] = m_instances.size();
    status["running"] = m_running ? true : false;
    
    int available = 0;
    int inUse = 0;
    int failed = 0;
    
    json instances = json::array();
    
    for (const auto& info : m_instances) {
        if (info.failureCount >= 3) {
            failed++;
        }
        else if (info.inUse) {
            inUse++;
        }
        else {
            available++;
        }
        
        json instanceInfo;
        instanceInfo["host"] = info.host;
        instanceInfo["port"] = info.port;
        instanceInfo["status"] = info.failureCount >= 3 ? "failed" : 
                                (info.inUse ? "in_use" : "available");
        instanceInfo["failure_count"] = info.getFailureCount();
        
        auto now = std::chrono::steady_clock::now();
        auto lastUseSeconds = std::chrono::duration_cast<std::chrono::seconds>(
                                now - info.lastUseTime).count();
        instanceInfo["last_use_seconds_ago"] = lastUseSeconds;
        
        // 检查实例指针是否为空
        bool isNull = (info.instance == nullptr);
        instanceInfo["is_null"] = isNull;
        
        // 检查实例可用性
        if (!isNull) {
            try {
                bool isAvailable = info.instance->isAvailable();
                instanceInfo["is_available"] = isAvailable;
            }
            catch (const std::exception& e) {
                instanceInfo["is_available"] = false;
                instanceInfo["error"] = e.what();
            }
        }
        
        instances.push_back(instanceInfo);
    }
    
    status["available"] = available;
    status["in_use"] = inUse;
    status["failed"] = failed;
    status["instances"] = instances;
    
    std::string statusJson = status.dump(2);
    if (m_instances.empty()) {
        std::cout << "警告: 实例池为空! 状态: " << statusJson << std::endl;
    }
    
    return statusJson;
}

void ModelInstancePool::shutdown() {
    m_running = false;
    
    if (m_healthCheckThread.joinable()) {
        m_healthCheckThread.join();
    }
    
    std::lock_guard<std::mutex> lock(m_poolMutex);
    m_instances.clear();
}

} // namespace services
} // namespace kama
