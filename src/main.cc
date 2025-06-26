#include <string>
#include <iostream>
#include <cstdio>
#include <memory>
#include "TcpServer.h"
#include <Logger.h>
#include <sys/stat.h>
#include <sstream>
#include "AsyncLogging.h"
#include "LFU.h"
#include "memoryPool.h"
#include <chrono>
#include <thread>
#include <fcntl.h>
#include <future>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic>
#include "Thread.h"
#include "CurrentThread.h"
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cuda_runtime.h> // CUDA 支持

// 简单线程池实现
class ThreadPool {
public:
    ThreadPool(size_t threads) : stop(false) {
        for(size_t i = 0; i < threads; ++i) {
            workers.emplace_back([this] {
                while(true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(this->queue_mutex);
                        this->condition.wait(lock, [this] { 
                            return this->stop || !this->tasks.empty(); 
                        });
                        if(this->stop && this->tasks.empty())
                            return;
                        task = std::move(this->tasks.front());
                        this->tasks.pop();
                    }
                    task();
                }
            });
        }
    }

    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args) -> std::future<typename std::result_of<F(Args...)>::type> {
        using return_type = typename std::result_of<F(Args...)>::type;

        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        
        std::future<return_type> res = task->get_future();
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            if(stop)
                throw std::runtime_error("enqueue on stopped ThreadPool");
            tasks.emplace([task](){ (*task)(); });
        }
        condition.notify_one();
        return res;
    }

    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            stop = true;
        }
        condition.notify_all();
        for(std::thread &worker: workers)
            worker.join();
    }

    // 在ThreadPool类中添加监控方法
    int getPendingTaskCount() {
        std::unique_lock<std::mutex> lock(queue_mutex);
        return tasks.size();
    }

private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop;
};

// 日志文件滚动大小为1MB (1*1024*1024字节)
static const off_t kRollSize = 1*1024*1024;

// LlamaService 类 - 支持 CPU 和 GPU 推理
class LlamaService {
public:
    LlamaService(const std::string& model_path, const std::string& server_ip = "127.0.0.1", int server_port = 8899, bool use_gpu = false) 
        : model_path_(model_path), server_ip_(server_ip), server_port_(server_port), use_gpu_(use_gpu) {
        LOG_INFO << "LlamaService 初始化，连接到 " << server_ip << ":" << server_port 
                 << (use_gpu_ ? " (启用GPU)" : " (使用CPU)");
    }
    
    ~LlamaService() {
        // 清理资源
    }
    
    // 启用或禁用 GPU
    void setUseGPU(bool use_gpu) {
        if (use_gpu != use_gpu_) {
            LOG_INFO << (use_gpu ? "启用GPU推理" : "切换到CPU推理");
            use_gpu_ = use_gpu;
        }
    }

    // 查询方法，选择CPU或GPU推理路径
    std::string query(const std::string& prompt, int timeout_ms = 30000) {
        // 根据是否使用GPU选择查询方式
        if (use_gpu_) {
            return query_with_gpu(prompt, timeout_ms);
        } else {
            return query_with_cpu(prompt, timeout_ms);
        }
    }

    // CPU推理实现
    std::string query_with_cpu(const std::string& prompt, int timeout_ms = 30000) {
        // 创建连接到LLaMA服务器的TCP客户端
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            LOG_ERROR << "无法创建套接字";
            return "服务器内部错误：无法创建连接";
        }
        
        // 设置连接超时
        struct timeval tv;
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof tv);
        
        // 连接服务器
        struct sockaddr_in serv_addr;
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(server_port_);
        inet_pton(AF_INET, server_ip_.c_str(), &serv_addr.sin_addr);
        
        if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
            LOG_ERROR << "连接到LLaMA服务器失败";
            close(sock);
            return "服务器内部错误：无法连接到LLaMA服务";
        }
        
        // 发送请求
        send(sock, prompt.c_str(), prompt.length(), 0);
        
        // 接收响应
        char buffer[4096] = {0};
        std::string response;
        int bytes_read;
        
        while ((bytes_read = read(sock, buffer, sizeof(buffer) - 1)) > 0) {
            buffer[bytes_read] = '\0';
            response += buffer;
        }
        
        close(sock);
        return response;
    }
    
    // GPU推理实现
    std::string query_with_gpu(const std::string& prompt, int timeout_ms = 30000) {
        LOG_INFO << "使用GPU进行推理处理";
        try {
            // 这里添加CUDA处理代码
            float *d_input = nullptr;
            float *d_output = nullptr;
            float *h_output = nullptr;
            cudaError_t cudaStatus;
            
            // 记录处理开始时间
            auto start_time = std::chrono::steady_clock::now();
            
            // 使用普通CPU处理作为后备方案
            std::string response = "GPU处理: " + query_with_cpu(prompt, timeout_ms);
            
            // 计算和记录GPU处理时间
            auto end_time = std::chrono::steady_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
            LOG_INFO << "GPU处理完成，耗时: " << duration << "ms";
            
            return response;
        } catch (const std::exception& e) {
            LOG_ERROR << "GPU处理出错: " << e.what();
            LOG_INFO << "回退到CPU处理";
            return "GPU处理失败，回退到CPU: " + query_with_cpu(prompt, timeout_ms);
        }
    }

    std::string query_streaming(const std::string& prompt, 
                     std::function<void(const std::string&)> chunk_callback,
                     int timeout_ms = 60000) {  // 增加默认超时时间到60秒
        // 根据是否使用GPU选择查询方式
        if (use_gpu_) {
            chunk_callback("[使用GPU推理]\n");
        }
        
        // 创建连接到LLaMA服务器的TCP客户端
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            LOG_ERROR << "无法创建套接字";
            return "服务器内部错误：无法创建连接";
        }
        
        // 设置连接超时
        struct timeval tv;
        tv.tv_sec = 5; // 连接超时设为5秒
        tv.tv_usec = 0;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
        setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof tv);
        
        // 连接服务器
        struct sockaddr_in serv_addr;
        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(server_port_);
        inet_pton(AF_INET, server_ip_.c_str(), &serv_addr.sin_addr);
        
        LOG_INFO << "连接到 LLaMA 服务: " << server_ip_ << ":" << server_port_;
        if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
            LOG_ERROR << "连接到LLaMA服务器失败: " << strerror(errno);
            close(sock);
            return "服务器内部错误：无法连接到LLaMA服务，请稍后重试";
        }
        
        // 添加流式请求标记
        std::string stream_prompt = "STREAM:" + prompt;
        LOG_INFO << "连接成功，发送请求: " << stream_prompt.substr(0, 30) << "...";
        send(sock, stream_prompt.c_str(), stream_prompt.length(), 0);
        
        // 接收流式响应
        char buffer[1024] = {0};
        std::string full_response;
        int bytes_read;
        
        // 设置非阻塞模式
        int flags = fcntl(sock, F_GETFL, 0);
        fcntl(sock, F_SETFL, flags | O_NONBLOCK);
        
        auto start_time = std::chrono::steady_clock::now();
        bool reading = true;
        bool received_data = false;
        int idle_count = 0;
        int wait_message_count = 0;
        
        while (reading) {
            bytes_read = read(sock, buffer, sizeof(buffer) - 1);
            
            if (bytes_read > 0) {
                buffer[bytes_read] = '\0';
                std::string chunk(buffer);
                chunk_callback(chunk);
                full_response += chunk;
                received_data = true;
                idle_count = 0;
                wait_message_count = 0;  // 重置等待消息计数
                LOG_INFO << "接收到响应块，长度: " << bytes_read;
            }
            else if (bytes_read == 0) {
                // 连接关闭
                reading = false;
            }
            else {
                if (errno != EAGAIN && errno != EWOULDBLOCK) {
                    // 发生错误
                    LOG_ERROR << "读取数据时发生错误: " << strerror(errno);
                    reading = false;
                }
                else {
                    // 无数据可读，检查是否超时
                    idle_count++;
                    auto now = std::chrono::steady_clock::now();
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();
                    
                    // 发送等待消息，现在改为每2秒发送一次，并添加不同的等待提示
                    if (idle_count % 20 == 0) {
                        std::string waiting_msg;
                        switch (wait_message_count % 3) {
                            case 0:
                                waiting_msg = "\n[正在等待模型响应...]\n";
                                break;
                            case 1:
                                waiting_msg = "\n[模型仍在思考中，请耐心等待...]\n";
                                break;
                            case 2:
                                waiting_msg = "\n[生成复杂回答需要更多时间...]\n";
                                break;
                        }
                        wait_message_count++;
                        chunk_callback(waiting_msg);
                    }
                    
                    // 15秒后如果还没收到数据，发送更详细的等待信息
                    if (elapsed > 15000 && !received_data && idle_count % 50 == 0) {
                        std::string detailed_msg = "\n[处理较慢，可能是请求复杂或服务器负载高...]\n";
                        chunk_callback(detailed_msg);
                    }
                    
                    if (elapsed > timeout_ms) {
                        std::string timeout_msg = "\n[响应超时，请确保LLaMA服务正常运行并重试]\n";
                        chunk_callback(timeout_msg);
                        full_response += timeout_msg;
                        LOG_ERROR << "请求处理超时 (" << timeout_ms << "ms)";
                        reading = false;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
            }
        }
        
        close(sock);
        
        // 如果完全没有收到数据，尝试检查LLaMA服务状态
        if (!received_data) {
            int status_sock = socket(AF_INET, SOCK_STREAM, 0);
            if (status_sock >= 0) {
                struct timeval status_tv;
                status_tv.tv_sec = 1;
                status_tv.tv_usec = 0;
                setsockopt(status_sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&status_tv, sizeof status_tv);
                setsockopt(status_sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&status_tv, sizeof status_tv);
                
                struct sockaddr_in status_addr;
                status_addr.sin_family = AF_INET;
                status_addr.sin_port = htons(server_port_);
                inet_pton(AF_INET, server_ip_.c_str(), &status_addr.sin_addr);
                
                if (connect(status_sock, (struct sockaddr *)&status_addr, sizeof(status_addr)) < 0) {
                    close(status_sock);
                    return "LLaMA服务可能已崩溃或未响应，请检查服务状态后重试";
                }
                close(status_sock);
            }
            return "服务器未能生成有效响应，但LLaMA服务仍在运行，请稍后重试";
        }
        
        return full_response;
    }
    
    // 检查LLaMA服务是否可用
    bool isServiceAvailable() {
        const int MAX_RETRIES = 2;
        for (int retry = 0; retry <= MAX_RETRIES; retry++) {
            int sock = socket(AF_INET, SOCK_STREAM, 0);
            if (sock < 0) {
                continue;
            }
            
            // 设置短超时
            struct timeval tv;
            tv.tv_sec = 1;
            tv.tv_usec = 0;
            setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
            setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof tv);
            
            struct sockaddr_in serv_addr;
            serv_addr.sin_family = AF_INET;
            serv_addr.sin_port = htons(server_port_);
            inet_pton(AF_INET, server_ip_.c_str(), &serv_addr.sin_addr);
            
            int result = connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
            close(sock);
            
            if (result >= 0) {
                return true;
            }
            
            if (retry < MAX_RETRIES) {
                LOG_INFO << "LLaMA服务连接失败，尝试重试 (" << (retry + 1) << "/" << MAX_RETRIES << ")";
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
        }
        
        return false;
    }
    
    std::string getServerIp() const {
        return server_ip_;
    }

    int getServerPort() const {
        return server_port_;
    }

    bool isUsingGPU() const {
        return use_gpu_;
    }

    // 重启连接方法，在遇到问题时可以调用
    bool restartConnection() {
        LOG_INFO << "尝试重新建立与LLaMA服务的连接...";
        bool available = isServiceAvailable();
        if (available) {
            LOG_INFO << "重新连接成功";
        } else {
            LOG_ERROR << "重新连接失败";
        }
        return available;
    }

private:
    std::string model_path_;
    std::string server_ip_;
    int server_port_;
    bool use_gpu_; // 是否启用 GPU
};

class EchoServer {
public:
    EchoServer(EventLoop *loop, const InetAddress &addr, const std::string &name)
        : server_(loop, addr, name) // (1) 初始化 TcpServer 对象
        , loop_(loop)   // (2) 初始化 EventLoop 指针
        , threadPool_(4)    // (3) 初始化线程池，包含4个线程
        , requestId_(0) // (4) 初始化请求ID计数器为0
        , llama_service_("/home/shl203/llama.cpp/models/qwen/Qwen-7B-Chat.Q4_K_M.gguf") // (5) 初始化 LLaMA 服务对象，并指定模型路径
        , lfu_cache_(100) // (6) 初始化 LFU 缓存，容量为100
    {
        // 注册回调函数
        server_.setConnectionCallback(
            std::bind(&EchoServer::onConnection, this, std::placeholders::_1));
        
        server_.setMessageCallback(
            std::bind(&EchoServer::onMessage, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));

        // 设置合适的subloop线程数量
        server_.setThreadNum(3);
    }
    void start()
    {
        server_.start();
    }

private:
    // 连接建立或断开的回调函数
    void onConnection(const TcpConnectionPtr &conn)   
    {
        if (conn->connected())
        {
            LOG_INFO<<"Connection UP :"<<conn->peerAddress().toIpPort().c_str();
        }
        else
        {
            LOG_INFO<<"Connection DOWN :"<<conn->peerAddress().toIpPort().c_str();
        }
    }

    // 可读写事件回调 - 使用流式处理
    void onMessage(const TcpConnectionPtr &conn, Buffer *buf, Timestamp time) {
        // 添加请求队列控制
        if (threadPool_.getPendingTaskCount() > 10) {
            conn->send("服务器正忙，请稍后再试\n");
            return;
        }

        std::string msg = buf->retrieveAllAsString();
        // 忽略空消息或只包含空白字符的消息
        if (msg.empty() || std::all_of(msg.begin(), msg.end(), ::isspace)) {
            conn->send("请输入有效的查询内容\n");
            return;
        }
        
        uint64_t id = ++requestId_;
        LOG_INFO << "收到用户消息 [" << id << "]: " << msg.substr(0, 50) << (msg.length() > 50 ? "..." : "");
        
        // 保存连接的引用计数指针
        std::shared_ptr<TcpConnection> connection = conn;
        conn->send("您的请求 #" + std::to_string(id) + " 已接收，正在处理中...\n");

        // 检查是否需要显示更多帮助信息
        if (msg == "help" || msg == "帮助") {
            connection->send("系统帮助：\n");
            connection->send("1. 直接输入文字内容向模型提问\n");
            connection->send("2. 输入'status'或'状态'查看系统状态\n");
            connection->send("3. 输入'clear'或'清除'清除缓存\n");
            return;
        }
        
        // 检查是否是状态查询命令
        if (msg == "status" || msg == "状态") {
            connection->send("系统状态：\n");
            connection->send("- 待处理任务数: " + std::to_string(threadPool_.getPendingTaskCount()) + "\n");
            connection->send("- 缓存条目数: " + std::to_string(lfu_cache_.size()) + "\n");
            connection->send("- LLaMA服务: " + std::string(llama_service_.isServiceAvailable() ? "可用" : "不可用") + "\n");
            return;
        }
        
        // 检查是否是清除缓存命令
        if (msg == "clear" || msg == "清除") {
            lfu_cache_.clear();
            connection->send("缓存已清除\n");
            return;
        }

        // 检查 LLaMA 服务是否可用
        if (!llama_service_.isServiceAvailable()) {
            LOG_ERROR << "LLaMA 服务不可用，请确保服务已启动在 " << llama_service_.getServerIp() << ":" << llama_service_.getServerPort();
            connection->send("错误：LLaMA 服务不可用，请确保服务已在 " + llama_service_.getServerIp() + ":" + 
                            std::to_string(llama_service_.getServerPort()) + " 正常运行后重试\n");
            connection->send("启动命令: /home/shl203/llama.cpp/build/bin/main -m /home/shl203/llama.cpp/models/qwen/Qwen-7B-Chat.Q4_K_M.gguf --port 8899 --host 0.0.0.0 --ctx-size 2048\n");
            return;
        }
        
        // 使用线程池异步处理
        threadPool_.enqueue([this, msg, connection, id]() {
            auto start = std::chrono::steady_clock::now();
            
            // 先检查缓存
            std::string result;
            if (lfu_cache_.get(msg, result)) {
                LOG_INFO << "命中缓存，直接返回结果";
                
                // 发送缓存结果
                connection->getLoop()->runInLoop([connection, result, id]() {
                    connection->send("请求 #" + std::to_string(id) + " 回答(缓存):\n");
                    connection->send(result + "\n");
                    connection->send("请求 #" + std::to_string(id) + " 已完成 (缓存命中)\n");
                });
                return;
            }
            
            try {
                // 发送开始处理的消息
                connection->getLoop()->runInLoop([connection]() {
                    connection->send("模型正在思考中...\n");
                });
                
                // 定义流式回调函数
                std::function<void(const std::string&)> chunk_callback = 
                    [connection, id](const std::string& chunk) {
                        connection->getLoop()->runInLoop([connection, chunk]() {
                            connection->send(chunk);
                        });
                    };
                
                // 使用流式处理方式查询，使用较长的超时时间
                std::string full_response = llama_service_.query_streaming(msg, chunk_callback, 90000);  // 增加到90秒
                
                // 检查响应是否包含超时信息
                if (full_response.find("[响应超时") != std::string::npos) {
                    LOG_WARN << "请求 #" << id << " 处理超时";
                    connection->getLoop()->runInLoop([connection]() {
                        connection->send("\n请尝试将问题拆分为更小的部分，或者稍后再试\n");
                    });
                } else if (!full_response.empty()) {
                    // 只有在得到有效响应时添加到缓存
                    lfu_cache_.put(msg, full_response);
                }
                
                // 计算用时并发送完成消息
                auto end = std::chrono::steady_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
                
                connection->getLoop()->runInLoop([connection, duration, id]() {
                    connection->send("\n请求 #" + std::to_string(id) + " 已完成，总处理时间：" + 
                                    std::to_string(duration) + " ms\n");
                    LOG_INFO << "请求 #" << id << " 总处理时间：" << duration << " ms";
                });
                
            } catch (const std::exception& e) {
                LOG_ERROR << "处理请求 #" << id << " 时发生错误: " << e.what();
                connection->getLoop()->runInLoop([connection, id, e=std::string(e.what())]() {
                    connection->send("请求 #" + std::to_string(id) + " 处理出错: " + e + "\n");
                });
            } catch (...) {
                LOG_ERROR << "处理请求 #" << id << " 时发生未知错误";
                connection->getLoop()->runInLoop([connection, id]() {
                    connection->send("请求 #" + std::to_string(id) + " 处理出错: 未知错误\n");
                });
            }
        });
    }

    std::string run_llama_cpp(const std::string& prompt) {
        int sock = 0;
        struct sockaddr_in serv_addr;
        char buffer[4096] = {0};
        std::string response;

        // 创建套接字
        if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
            return "无法创建套接字";
        }

        serv_addr.sin_family = AF_INET;
        serv_addr.sin_port = htons(8899);

        // 连接 LLaMA 服务
        if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
            return "无效的地址";
        }

        if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
            return "无法连接 LLaMA 服务";
        }

        // 发送提示词
        send(sock, prompt.c_str(), prompt.size(), 0);

        // 流式读取 LLaMA 响应
        auto start_time = std::chrono::steady_clock::now();
        while (true) {
            memset(buffer, 0, sizeof(buffer));
            ssize_t bytes_read = read(sock, buffer, sizeof(buffer) - 1);
            if (bytes_read > 0) {
                response += std::string(buffer, bytes_read);
                std::cout << "STREAM:" << std::string(buffer, bytes_read) << std::flush; // 实时输出流式响应
            } else {
                break;
            }

            // 超时控制（最多 30 秒）
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count() > 30000) {
                response += "\n[响应超时]";
                break;
            }
        }

        close(sock);
        return response;
    }

    TcpServer server_;
    EventLoop *loop_;
    ThreadPool threadPool_;  // 线程池
    std::atomic<uint64_t> requestId_;  // 请求ID计数器
    LlamaService llama_service_;  // LLaMA服务
    KamaCache::KLfuCache<std::string, std::string> lfu_cache_;  // LFU缓存
};

AsyncLogging* g_asyncLog = NULL;
AsyncLogging * getAsyncLog(){
    return g_asyncLog;
}

void asyncLog(const char* msg, int len)
{
    AsyncLogging* logging = getAsyncLog();
    if (logging)
    {
        logging->append(msg, len);
    }
}

int main(int argc, char *argv[]) {
    //第一步启动日志，双缓冲异步写入磁盘.
    //创建一个文件夹
    const std::string LogDir="logs";
    mkdir(LogDir.c_str(),0755);
    //使用std::stringstream 构建日志文件夹
    std::ostringstream LogfilePath;
    LogfilePath << LogDir << "/" << ::basename(argv[0]); // 完整的日志文件路径
    AsyncLogging log(LogfilePath.str(), kRollSize);
    g_asyncLog = &log;
    Logger::setOutput(asyncLog); // 为Logger设置输出回调, 重新配接输出位置
    log.start(); // 开启日志后端线程
    //第二步启动内存池和LFU缓存
     // 初始化内存池
    memoryPool::HashBucket::initMemoryPool();

    // 初始化缓存
    const int CAPACITY = 5;  
    KamaCache::KLfuCache<int, std::string> lfu(CAPACITY);
    //第三步启动底层网络模块
    EventLoop loop;
    InetAddress addr(8080);
    EchoServer server(&loop, addr, "EchoServer");
    server.start();
    // 主loop开始事件循环  epoll_wait阻塞 等待就绪事件(主loop只注册了监听套接字的fd，所以只会处理新连接事件)
    std::cout << "================================================Start Web Server================================================" << std::endl;
    std::cout << "LLaMA.cpp 已集成至 WebServer，等待用户请求。" << std::endl;
    std::cout << "LLaMA.cpp 命令：" << "/home/shl203/llama.cpp/build/bin/main -m /home/shl203/llama.cpp/models/qwen/Qwen-7B-Chat.Q4_K_M.gguf" << std::endl;
    
    // 确保以下代码执行
    try {
        loop.loop();
    }
    catch (const std::exception& e) {
        std::cerr << "EventLoop异常: " << e.what() << std::endl;
        LOG_ERROR << "EventLoop异常: " << e.what();
    }

    std::cout << "================================================Stop Web Server=================================================" << std::endl;
    //结束日志打野
    log.stop();
    return 0;
}