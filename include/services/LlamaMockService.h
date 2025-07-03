#pragma once

#include "services/ILlamaService.h"
#include "noncopyable.h"
#include <string>
#include <memory>
#include <chrono>
#include <thread>
#include <random>
#include <stdexcept>

namespace kama {
namespace services {

/**
 * @brief LLaMA模拟服务
 * 
 * 用于测试模型实例池和异步服务，不需要实际的LLaMA服务
 */
class LlamaMockService : public ILlamaService {
public:
    /**
     * @brief 构造函数
     * 
     * @param delayMs 模拟处理延迟(毫秒)
     * @param failRate 模拟失败率(0-100)
     */
    LlamaMockService(int delayMs = 100, int failRate = 0) 
        : m_delayMs(delayMs), m_failRate(failRate), m_available(true) {
        // 初始化随机数生成器
        m_randomEngine.seed(std::chrono::system_clock::now().time_since_epoch().count());
    }
    
    /**
     * @brief 查询接口
     * 
     * @param prompt 输入提示
     * @return 响应结果
     */
    std::string query(const std::string& prompt) override {
        // 模拟处理延迟
        std::this_thread::sleep_for(std::chrono::milliseconds(m_delayMs));
        
        // 随机模拟失败
        std::uniform_int_distribution<int> dist(1, 100);
        if (dist(m_randomEngine) <= m_failRate) {
            throw std::runtime_error("模拟服务故障");
        }
        
        // 构造简单响应
        return "模拟响应: " + prompt.substr(0, 10) + "... [处理用时: " + 
               std::to_string(m_delayMs) + "ms]";
    }
    
    /**
     * @brief 检查服务是否可用
     */
    bool isAvailable() const override {
        return m_available;
    }
    
    /**
     * @brief 重置连接
     */
    bool resetConnection() override {
        // 模拟重置操作
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        m_available = true;
        return true;
    }
    
    /**
     * @brief 设置可用状态
     */
    void setAvailable(bool available) {
        m_available = available;
    }
    
private:
    int m_delayMs;
    int m_failRate;
    bool m_available;
    std::default_random_engine m_randomEngine;
};

/**
 * @brief 创建模拟LLaMA服务工厂
 */
class LlamaMockServiceFactory {
public:
    /**
     * @brief 创建模拟服务实例
     */
    static std::shared_ptr<ILlamaService> create(int delayMs = 100, int failRate = 0) {
        return std::make_shared<LlamaMockService>(delayMs, failRate);
    }
};

} // namespace services
} // namespace kama
