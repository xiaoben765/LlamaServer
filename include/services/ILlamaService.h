#pragma once

#include <string>

namespace kama {
namespace services {

/**
 * @brief LLaMA服务接口
 * 
 * 定义了与LLaMA模型交互的标准接口，
 * 允许不同的实现（如TCP客户端、直接调用等）
 */
class ILlamaService {
public:
    virtual ~ILlamaService() = default;
    
    /**
     * @brief 查询LLaMA模型
     * 
     * @param prompt 输入提示词
     * @return 模型生成的响应
     */
    virtual std::string query(const std::string& prompt) = 0;
    
    /**
     * @brief 检查服务是否可用
     * 
     * @return 服务可用状态
     */
    virtual bool isAvailable() const = 0;
    
    /**
     * @brief 尝试重置连接
     * 
     * @return 重置是否成功
     */
    virtual bool resetConnection() = 0;
};

} // namespace services
} // namespace kama
