#pragma once

#include "services/ILlamaService.h"
#include "services/ModelInstancePool.h"
#include "AsyncTaskQueue.h"
#include <string>
#include <future>
#include <functional>

namespace llama {
namespace services {

/**
 * @brief 异步LLaMA服务
 * 
 * 提供非阻塞的LLaMA服务API，将请求异步处理
 */
class AsyncLlamaService : public ILlamaService {
public:
    /**
     * @brief 构造函数
     * 
     * @param timeout 超时时间（毫秒），0表示无超时
     */
    explicit AsyncLlamaService(unsigned int timeout = 30000);
    ~AsyncLlamaService() override;
    
    /**
     * @brief 同步查询LLaMA模型
     * 
     * @param prompt 输入提示词
     * @return 模型生成的响应
     */
    std::string query(const std::string& prompt) override;
    
    /**
     * @brief 异步查询LLaMA模型
     * 
     * @param prompt 输入提示词
     * @return std::future<std::string> 异步结果
     */
    std::future<std::string> queryAsync(const std::string& prompt);
    
    /**
     * @brief 设置完成回调函数
     * 
     * @param callback 回调函数，接受prompt和result两个参数
     */
    void setCompletionCallback(std::function<void(const std::string&, const std::string&)> callback);
    
    /**
     * @brief 检查服务是否可用
     * 
     * @return 服务可用状态
     */
    bool isAvailable() const override;
    
    /**
     * @brief 尝试重置连接
     * 
     * @return 重置是否成功
     */
    bool resetConnection() override;

private:
    // 内部查询实现
    std::string queryInternal(const std::string& prompt);
    
private:
    unsigned int m_timeout;
    std::function<void(const std::string&, const std::string&)> m_completionCallback;
};

} // namespace services
} // namespace llama
