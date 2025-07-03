#include "AsyncTaskQueue.h"
#include "services/ModelInstancePool.h"
#include "services/AsyncLlamaService.h"
#include "http/HighConcurrentHttpServer.h"
#include "EventLoop.h"
#include "InetAddress.h"

#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <functional>

using namespace kama;
using namespace kama::services;
using namespace kama::http;

// 测试异步任务队列
void testAsyncTaskQueue() {
    std::cout << "===== 测试异步任务队列 =====" << std::endl;
    
    // 初始化任务队列
    auto& taskQueue = AsyncTaskQueue::getInstance();
    taskQueue.init(4); // 使用4个线程
    
    // 提交一些任务
    std::vector<std::future<int>> results;
    
    auto start = std::chrono::steady_clock::now();
    
    for (int i = 0; i < 10; ++i) {
        auto future = taskQueue.submit([i]() -> int {
            // 模拟耗时操作
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            return i * i;
        });
        
        results.push_back(std::move(future));
    }
    
    // 收集结果
    for (auto& future : results) {
        std::cout << "任务结果: " << future.get() << std::endl;
    }
    
    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    
    std::cout << "总耗时: " << duration << "ms" << std::endl;
    std::cout << "理想情况下，10个任务串行执行需要1000ms，并行执行(4线程)约需250ms" << std::endl;
    
    taskQueue.shutdown();
}

// 测试模型实例池
void testModelInstancePool() {
    std::cout << "\n===== 测试模型实例池 =====" << std::endl;
    
    // 初始化模型实例池
    auto& pool = ModelInstancePool::getInstance();
    
    // 首先尝试使用真实服务
    std::vector<std::string> hosts = {"127.0.0.1:8899"};
    bool success = pool.init("/path/to/model", hosts, 2);
    
    // 如果真实服务不可用，则使用模拟服务
    if (!success) {
        std::cout << "真实LLaMA服务不可用，使用模拟服务代替..." << std::endl;
        success = pool.initWithMockServices(5, 100, 10);
    }
    
    if (success) {
        std::cout << "模型实例池初始化成功" << std::endl;
        
        // 获取池状态
        std::cout << pool.getPoolStatus() << std::endl;
        
        // 模拟获取实例和释放
        auto instance1 = pool.getAvailableInstance();
        if (instance1) {
            std::cout << "成功获取实例1" << std::endl;
            pool.releaseInstance(instance1);
        }
        
        auto instance2 = pool.getAvailableInstance();
        if (instance2) {
            std::cout << "成功获取实例2" << std::endl;
            pool.releaseInstance(instance2);
        }
        
        // 模拟故障报告
        if (instance1) {
            pool.reportInstanceFailure(instance1);
            std::cout << "已报告实例1故障" << std::endl;
            
            // 获取最新状态
            std::cout << pool.getPoolStatus() << std::endl;
        }
        
        pool.shutdown();
    } else {
        std::cout << "模型实例池初始化失败，可能是没有可用的服务实例" << std::endl;
    }
}

// 测试异步LLaMA服务
void testAsyncLlamaService() {
    std::cout << "\n========== 测试异步LLaMA服务 ==========" << std::endl;
    
    // 确保初始化异步任务队列（AsyncLlamaService依赖它）
    auto& taskQueue = AsyncTaskQueue::getInstance();
    if (!taskQueue.isRunning()) {
        std::cout << "初始化异步任务队列..." << std::endl;
        taskQueue.init(4);
    }
    
    // 确保模型实例池已初始化
    auto& pool = ModelInstancePool::getInstance();
    
    std::cout << "检查模型实例池状态..." << std::endl;
    std::string poolStatus = pool.getPoolStatus();
    
    // 不管之前状态如何，都重新初始化模型池
    std::cout << "关闭并重置模型实例池..." << std::endl;
    pool.shutdown();
    
    std::cout << "使用模拟服务初始化模型实例池..." << std::endl;
    bool initSuccess = pool.initWithMockServices(3, 100, 0); // 使用0%的失败率以便测试
    std::cout << "初始化模拟服务结果: " << (initSuccess ? "成功" : "失败") << std::endl;
    
    if (!initSuccess) {
        std::cout << "模型实例池初始化失败，测试无法继续" << std::endl;
        return;
    }
    
    // 验证实例池状态
    poolStatus = pool.getPoolStatus();
    std::cout << "初始化后的模型实例池状态: " << poolStatus << std::endl;
    
    // 尝试获取一个实例并测试
    {
        std::cout << "\n直接测试模型实例池:" << std::endl;
        auto instance = pool.getAvailableInstance();
        if (instance) {
            std::cout << "成功获取模型实例" << std::endl;
            try {
                std::string result = instance->query("测试查询");
                std::cout << "模型实例直接查询结果: " << result << std::endl;
            }
            catch (const std::exception& e) {
                std::cout << "模型实例直接查询异常: " << e.what() << std::endl;
            }
            
            // 释放实例
            pool.releaseInstance(instance);
            std::cout << "已释放模型实例" << std::endl;
        } else {
            std::cout << "无法获取模型实例，异步服务测试可能会失败" << std::endl;
        }
    }
    
    // 创建异步服务
    std::cout << "\n创建异步LLaMA服务实例..." << std::endl;
    AsyncLlamaService asyncService(5000); // 5秒超时
    
    // 设置回调
    asyncService.setCompletionCallback([](const std::string& prompt, const std::string& result) {
        std::cout << "收到异步回调 - 提示词: \"" << prompt.substr(0, 20) 
                  << (prompt.length() > 20 ? "..." : "") 
                  << "\", 响应: \"" << result << "\"" << std::endl;
    });
    
    // 测试异步查询
    std::cout << "检查异步服务可用性..." << std::endl;
    bool serviceAvailable = asyncService.isAvailable();
    std::cout << "异步服务可用性: " << (serviceAvailable ? "可用" : "不可用") << std::endl;
    
    if (serviceAvailable) {
        std::cout << "发送异步查询请求..." << std::endl;
        
        auto future = asyncService.queryAsync("你好，这是一个测试");
        
        // 等待结果
        std::cout << "等待异步结果..." << std::endl;
        try {
            std::string result = future.get();
            std::cout << "异步查询结果: " << result << std::endl;
        } catch (const std::exception& e) {
            std::cout << "异步查询失败: " << e.what() << std::endl;
        }
        
        std::cout << "异步查询测试完成" << std::endl;
    } else {
        std::cout << "服务不可用，无法执行异步查询" << std::endl;
        std::cout << "查看模型实例池最新状态: " << pool.getPoolStatus() << std::endl;
        
        // 调试信息
        std::cout << "\n--- 调试信息 ---" << std::endl;
        std::cout << "尝试直接获取实例，检查是否有可用实例..." << std::endl;
        auto instance = pool.getAvailableInstance();
        if (instance) {
            std::cout << "直接获取实例成功，这表明问题可能在AsyncLlamaService的isAvailable()方法中" << std::endl;
            pool.releaseInstance(instance);
        } else {
            std::cout << "直接获取实例也失败，确认模型实例池确实没有可用实例" << std::endl;
        }
    }
    
    // 关闭资源
    std::cout << "\n清理资源..." << std::endl;
    pool.shutdown();
    std::cout << "模型实例池已关闭" << std::endl;
}

// 测试高并发HTTP服务器
void testHighConcurrentHttpServer() {
    std::cout << "\n===== 测试高并发HTTP服务器 =====" << std::endl;
    
    EventLoop loop;
    
    // 实现端口自动递增功能
    int port = 8080;
    int maxPort = 8100; // 最多尝试20个端口
    bool serverStarted = false;
    HighConcurrentHttpServer* server = nullptr;
    
    while (port < maxPort && !serverStarted) {
        try {
            std::cout << "尝试使用端口: " << port << std::endl;
            InetAddress addr(port);
            server = new HighConcurrentHttpServer(&loop, addr, "TestServer", 4);
            serverStarted = true;
            std::cout << "成功使用端口 " << port << std::endl;
        } catch (const std::exception& e) {
            std::cout << "端口 " << port << " 不可用: " << e.what() << std::endl;
            delete server;
            server = nullptr;
            port++; // 尝试下一个端口
        }
    }
    
    if (!serverStarted || !server) {
        std::cerr << "错误: 无法启动HTTP服务器，所有端口 (8080-" << (maxPort-1) << ") 均被占用" << std::endl;
        return;
    }
    
    // 注册处理器
    server->registerHandler("/hello", [](const HttpRequest& req, HttpResponse& resp) {
        resp.setStatusCode(HttpStatusCode::OK);
        resp.setStatusMessage("OK");
        resp.setContentType("text/html");
        resp.addHeader("Custom-Header", "Value");
        resp.setBody("<h1>Hello World!</h1>");
    });
    
    // 注册异步处理器
    server->registerAsyncHandler("/async", [](const HttpRequest& req, HttpResponse& resp, 
                                           std::function<void(HttpResponse&)> callback) {
        // 在另一个线程中处理请求
        AsyncTaskQueue::getInstance().submit([req, resp, callback]() mutable {
            // 模拟耗时操作
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            resp.setStatusCode(HttpStatusCode::OK);
            resp.setStatusMessage("OK");
            resp.setContentType("text/html");
            resp.setBody("<h1>Async Response</h1><p>Processed in background</p>");
            
            // 完成处理，调用回调
            callback(resp);
        });
    });
    
    std::cout << "HTTP服务器已启动，监听端口" << port << std::endl;
    std::cout << "可以通过以下URL测试:" << std::endl;
    std::cout << "  - http://localhost:" << port << "/hello" << std::endl;
    std::cout << "  - http://localhost:" << port << "/async" << std::endl;
    std::cout << "按Ctrl+C退出..." << std::endl;
    
    server->start();
    loop.loop();
    
    // 清理资源
    delete server;
}

int main(int argc, char* argv[]) {
    std::cout << "===== 高并发处理测试程序 =====" << std::endl;
    
    // 默认测试所有功能
    bool testAll = true;
    bool testAsync = false;
    bool testPool = false;
    bool testService = false;
    bool testHttp = false;
    
    // 解析命令行参数
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--async") {
            testAsync = true;
            testAll = false;
        } else if (arg == "--pool") {
            testPool = true;
            testAll = false;
        } else if (arg == "--service") {
            testService = true;
            testAll = false;
        } else if (arg == "--http") {
            testHttp = true;
            testAll = false;
        } else if (arg == "--help") {
            std::cout << "用法: " << argv[0] << " [选项]" << std::endl;
            std::cout << "选项:" << std::endl;
            std::cout << "  --async    测试异步任务队列" << std::endl;
            std::cout << "  --pool     测试模型实例池" << std::endl;
            std::cout << "  --service  测试异步LLaMA服务" << std::endl;
            std::cout << "  --http     测试高并发HTTP服务器" << std::endl;
            std::cout << "  --help     显示此帮助信息" << std::endl;
            return 0;
        }
    }
    
    try {
        // 根据参数执行对应测试
        if (testAll || testAsync) {
            testAsyncTaskQueue();
        }
        
        if (testAll || testPool) {
            testModelInstancePool();
        }
        
        if (testAll || testService) {
            testAsyncLlamaService();
        }
        
        if (testAll || testHttp) {
            testHighConcurrentHttpServer();
        }
    }
    catch (const std::exception& e) {
        std::cerr << "测试过程中发生异常: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
