#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <memory>
#include <future>
#include <random>
#include <curl/curl.h>
#include <json/json.h>

class PerformanceTester {
private:
    std::string serverUrl_;
    std::atomic<int> successCount_{0};
    std::atomic<int> failureCount_{0};
    std::atomic<long long> totalResponseTime_{0};
    std::vector<long long> responseTimes_;
    std::mutex responseTimesMutex_;

    // CURL 写回调
    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* response) {
        size_t totalSize = size * nmemb;
        response->append((char*)contents, totalSize);
        return totalSize;
    }

public:
    PerformanceTester(const std::string& url) : serverUrl_(url) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
    }
    
    ~PerformanceTester() {
        curl_global_cleanup();
    }

    // 单个 HTTP 请求测试
    std::pair<bool, long long> performSingleRequest(const std::string& endpoint, 
                                                    const std::string& postData = "") {
        CURL* curl = curl_easy_init();
        if (!curl) {
            return {false, 0};
        }

        std::string response;
        std::string url = serverUrl_ + endpoint;
        auto startTime = std::chrono::high_resolution_clock::now();

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);

        if (!postData.empty()) {
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postData.c_str());
            struct curl_slist* headers = nullptr;
            headers = curl_slist_append(headers, "Content-Type: application/json");
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        }

        CURLcode res = curl_easy_perform(curl);
        
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();

        long responseCode = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);

        curl_easy_cleanup(curl);

        bool success = (res == CURLE_OK && responseCode == 200);
        return {success, duration};
    }

    // 并发测试工作线程
    void workerThread(int threadId, int requestsPerThread, const std::string& endpoint, 
                     const std::string& postData, std::atomic<bool>& stopFlag) {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(1, 10);

        for (int i = 0; i < requestsPerThread && !stopFlag; ++i) {
            auto [success, responseTime] = performSingleRequest(endpoint, postData);
            
            if (success) {
                successCount_++;
                totalResponseTime_ += responseTime;
                
                std::lock_guard<std::mutex> lock(responseTimesMutex_);
                responseTimes_.push_back(responseTime);
            } else {
                failureCount_++;
            }

            // 添加随机延迟模拟真实用户行为
            std::this_thread::sleep_for(std::chrono::milliseconds(dis(gen)));
        }
    }

    // 运行并发测试
    void runConcurrencyTest(int numThreads, int requestsPerThread, 
                           const std::string& endpoint, const std::string& postData = "") {
        std::cout << "开始并发测试: " << numThreads << " 线程, 每线程 " 
                  << requestsPerThread << " 请求" << std::endl;

        // 重置计数器
        successCount_ = 0;
        failureCount_ = 0;
        totalResponseTime_ = 0;
        responseTimes_.clear();

        std::vector<std::thread> threads;
        std::atomic<bool> stopFlag{false};
        auto startTime = std::chrono::high_resolution_clock::now();

        // 启动工作线程
        for (int i = 0; i < numThreads; ++i) {
            threads.emplace_back(&PerformanceTester::workerThread, this, 
                               i, requestsPerThread, endpoint, postData, std::ref(stopFlag));
        }

        // 等待所有线程完成
        for (auto& thread : threads) {
            thread.join();
        }

        auto endTime = std::chrono::high_resolution_clock::now();
        auto totalDuration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();

        // 输出结果
        printResults(totalDuration);
    }

    // 压力测试 - 逐步增加负载
    void runStressTest(const std::string& endpoint, const std::string& postData = "") {
        std::cout << "\n=== 压力测试 ===" << std::endl;
        
        std::vector<int> concurrencyLevels = {1, 5, 10, 20, 50, 100};
        
        for (int concurrency : concurrencyLevels) {
            std::cout << "\n--- 并发级别: " << concurrency << " ---" << std::endl;
            runConcurrencyTest(concurrency, 50, endpoint, postData);
            
            // 测试间隔
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
    }

    // 持续负载测试
    void runSustainedLoadTest(int numThreads, int durationSeconds, 
                             const std::string& endpoint, const std::string& postData = "") {
        std::cout << "\n=== 持续负载测试 (" << durationSeconds << " 秒) ===" << std::endl;

        successCount_ = 0;
        failureCount_ = 0;
        totalResponseTime_ = 0;
        responseTimes_.clear();

        std::vector<std::thread> threads;
        std::atomic<bool> stopFlag{false};
        auto startTime = std::chrono::high_resolution_clock::now();

        // 启动工作线程
        for (int i = 0; i < numThreads; ++i) {
            threads.emplace_back([this, &endpoint, &postData, &stopFlag]() {
                std::random_device rd;
                std::mt19937 gen(rd());
                std::uniform_int_distribution<> dis(50, 200);

                while (!stopFlag) {
                    auto [success, responseTime] = performSingleRequest(endpoint, postData);
                    
                    if (success) {
                        successCount_++;
                        totalResponseTime_ += responseTime;
                        
                        std::lock_guard<std::mutex> lock(responseTimesMutex_);
                        responseTimes_.push_back(responseTime);
                    } else {
                        failureCount_++;
                    }

                    std::this_thread::sleep_for(std::chrono::milliseconds(dis(gen)));
                }
            });
        }

        // 运行指定时间
        std::this_thread::sleep_for(std::chrono::seconds(durationSeconds));
        stopFlag = true;

        // 等待所有线程完成
        for (auto& thread : threads) {
            thread.join();
        }

        auto endTime = std::chrono::high_resolution_clock::now();
        auto totalDuration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();

        printResults(totalDuration);
    }

    // 打印测试结果
    void printResults(long long totalDurationMs) {
        int totalRequests = successCount_ + failureCount_;
        double successRate = totalRequests > 0 ? (double)successCount_ / totalRequests * 100 : 0;
        double qps = totalDurationMs > 0 ? (double)successCount_ / (totalDurationMs / 1000.0) : 0;
        double avgResponseTime = successCount_ > 0 ? (double)totalResponseTime_ / successCount_ : 0;

        std::cout << "\n--- 测试结果 ---" << std::endl;
        std::cout << "总请求数: " << totalRequests << std::endl;
        std::cout << "成功请求: " << successCount_ << std::endl;
        std::cout << "失败请求: " << failureCount_ << std::endl;
        std::cout << "成功率: " << std::fixed << std::setprecision(2) << successRate << "%" << std::endl;
        std::cout << "QPS: " << std::fixed << std::setprecision(2) << qps << std::endl;
        std::cout << "平均响应时间: " << std::fixed << std::setprecision(2) << avgResponseTime << " ms" << std::endl;
        std::cout << "总耗时: " << totalDurationMs << " ms" << std::endl;

        // 计算响应时间百分位数
        if (!responseTimes_.empty()) {
            std::lock_guard<std::mutex> lock(responseTimesMutex_);
            std::sort(responseTimes_.begin(), responseTimes_.end());
            
            size_t count = responseTimes_.size();
            std::cout << "响应时间分布:" << std::endl;
            std::cout << "  50%: " << responseTimes_[count * 0.5] << " ms" << std::endl;
            std::cout << "  90%: " << responseTimes_[count * 0.9] << " ms" << std::endl;
            std::cout << "  95%: " << responseTimes_[count * 0.95] << " ms" << std::endl;
            std::cout << "  99%: " << responseTimes_[count * 0.99] << " ms" << std::endl;
        }
    }
};

int main(int argc, char* argv[]) {
    std::string serverUrl = "http://127.0.0.1:8080";
    
    if (argc > 1) {
        serverUrl = argv[1];
    }

    std::cout << "Kama-WebServer 性能测试工具" << std::endl;
    std::cout << "服务器: " << serverUrl << std::endl;

    PerformanceTester tester(serverUrl);

    // 准备测试数据
    std::string chatPostData = R"({"message": "性能测试消息 - 这是一个用于测试的较长消息内容"})";

    try {
        // 1. 基本连通性测试
        std::cout << "\n=== 连通性测试 ===" << std::endl;
        auto [connected, responseTime] = tester.performSingleRequest("/api/status");
        if (connected) {
            std::cout << "服务器连接正常，响应时间: " << responseTime << " ms" << std::endl;
        } else {
            std::cout << "服务器连接失败！" << std::endl;
            return 1;
        }

        // 2. GET 接口并发测试
        std::cout << "\n=== GET 接口 (/api/status) 并发测试 ===" << std::endl;
        tester.runConcurrencyTest(10, 100, "/api/status");

        // 3. POST 接口并发测试
        std::cout << "\n=== POST 接口 (/api/chat) 并发测试 ===" << std::endl;
        tester.runConcurrencyTest(5, 50, "/api/chat", chatPostData);

        // 4. 压力测试
        tester.runStressTest("/api/status");

        // 5. 持续负载测试
        tester.runSustainedLoadTest(20, 60, "/api/status");

        std::cout << "\n=== 性能测试完成 ===" << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "测试过程中发生错误: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
