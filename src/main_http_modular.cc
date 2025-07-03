#include "http/HttpServer.h"
#include "http/Middleware.h"
#include "services/IDatabaseService.h"
#include "services/DatabaseService.h"
#include "services/ILlamaService.h"
#include "services/LlamaService.h"
#include "services/LlamaMockService.h"
#include "AsyncLogging.h"
#include "Logger.h"
#include "ConfigManager.h"
#include <iostream>
#include <memory>
#include <functional>
#include <string>
#include <nlohmann/json.hpp>

using namespace kama;
using namespace kama::http;
using json = nlohmann::json;

// 负责协调各个服务的应用类
class KamaHttpApplication {
public:
    KamaHttpApplication(
        EventLoop* loop, 
        const InetAddress& addr,
        const std::string& staticFilesRoot
    ) : 
        server_(loop, addr, "KamaHttpServer"),
        dbService_(nullptr),
        llamaService_(nullptr)
    {
        try {
            std::cout << "成员变量初始化完成" << std::endl;
            
            std::cout << "配置静态文件服务..." << std::endl;
            // 配置静态文件服务
            server_.setStaticFileRoot(staticFilesRoot);
            server_.enableStaticFiles(true);
            std::cout << "静态文件服务配置完成" << std::endl;
            
            // 读取配置
            std::cout << "读取配置..." << std::endl;
            loadConfig();
            std::cout << "配置读取完成" << std::endl;
            
            // 添加中间件
            std::cout << "设置中间件..." << std::endl;
            setupMiddleware();
            std::cout << "中间件设置完成" << std::endl;
            
            // 初始化服务
            std::cout << "初始化服务..." << std::endl;
            initServices();
            std::cout << "服务初始化完成" << std::endl;
            
            // 注册路由
            std::cout << "注册路由..." << std::endl;
            setupRoutes();
            std::cout << "路由注册完成" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "应用初始化发生异常: " << e.what() << std::endl;
            throw;
        } catch (...) {
            std::cerr << "应用初始化发生未知异常" << std::endl;
            throw;
        }
    }
    
    void start() {
        LOG_INFO << "启动Kama HTTP服务器, 端口: " << port_;
        server_.setThreadNum(threadNum_);
        server_.start();
    }
    
private:
    // 加载配置
    void loadConfig() {
        try {
            std::cout << "加载ConfigManager..." << std::endl;
            // 从ConfigManager加载配置，如果失败则使用默认值
            auto& config = ConfigManager::instance();
            
            std::cout << "初始化ConfigManager..." << std::endl;
            config.initialize(); // 确保配置初始化
            std::cout << "ConfigManager初始化完成" << std::endl;
            
            // 从配置中读取，或使用默认值
            std::cout << "读取http.port配置项..." << std::endl;
            port_ = config.get<int>("server.http_port", 8081); // 使用server.http_port，默认值改为8081
            std::cout << "http.port = " << port_ << std::endl;
            
            std::cout << "读取http.threads配置项..." << std::endl;
            threadNum_ = config.get<int>("http.threads", 4);
            std::cout << "http.threads = " << threadNum_ << std::endl;
            
            std::cout << "读取llama.service_type配置项..." << std::endl;
            llamaServiceType_ = config.get<std::string>("llama.service_type", "tcp");
            std::cout << "llama.service_type = " << llamaServiceType_ << std::endl;
            
            // 使用实际存在的LLaMA模型路径
            std::cout << "设置LLaMA模型路径..." << std::endl;
            llamaModelPath_ = "/home/shl203/llama.cpp/models/qwen/Qwen-7B-Chat.Q4_K_M.gguf";
            std::cout << "LLaMA模型路径 = " << llamaModelPath_ << std::endl;
            
            std::cout << "读取llama.server_ip配置项..." << std::endl;
            llamaServerIp_ = config.get<std::string>("llama.server_ip", "127.0.0.1");
            std::cout << "llama.server_ip = " << llamaServerIp_ << std::endl;
            
            std::cout << "读取llama.server_port配置项..." << std::endl;
            llamaServerPort_ = config.get<int>("llama.server_port", 8899);
            std::cout << "llama.server_port = " << llamaServerPort_ << std::endl;
            
            std::cout << "读取db.type配置项..." << std::endl;
            dbType_ = config.get<std::string>("db.type", "memory"); // 默认使用内存数据库，更安全
            std::cout << "db.type = " << dbType_ << std::endl;
            
            LOG_INFO << "配置加载完成: " 
                     << "端口=" << port_ 
                     << ", 线程数=" << threadNum_
                     << ", LLaMA服务类型=" << llamaServiceType_
                     << ", 数据库类型=" << dbType_;
        } catch (const std::exception& e) {
            std::cerr << "加载配置异常: " << e.what() << std::endl;
            LOG_ERROR << "加载配置失败: " << e.what() << "，使用默认配置";
            
            // 使用默认值
            std::cout << "使用默认配置..." << std::endl;
            port_ = 8080;
            threadNum_ = 4;
            llamaServiceType_ = "tcp";
            llamaModelPath_ = "/home/shl203/llama.cpp/models/qwen/Qwen-7B-Chat.Q4_K_M.gguf";
            llamaServerIp_ = "127.0.0.1";
            llamaServerPort_ = 8899;
            dbType_ = "memory";
            std::cout << "默认配置设置完成" << std::endl;
        }
    }
    
    // 设置中间件
    void setupMiddleware() {
        // 启用日志中间件
        server_.enableLogging();
        
        // 启用CORS
        server_.enableCors("*");
        
        // 启用速率限制 - 每分钟最多60个请求
        server_.enableRateLimit(60, 60);
        
        // 启用压缩
        server_.enableCompression();
        
        // 启用身份验证并配置需要认证的路径
        auto authMiddleware = server_.enableAuth();
        authMiddleware->addPath("/api/admin", true);
        authMiddleware->addPath("/api/users", true);
        // API请求不需要认证
        authMiddleware->addPath("/api/llama/query", false);
        authMiddleware->addPath("/api/status", false);
    }
    
    // 初始化服务
    void initServices() {
        try {
            // 初始化数据库服务
            std::cout << "初始化数据库服务..." << std::endl;
            std::cout << "数据库类型: " << dbType_ << std::endl;
            
            if (dbType_ == "mysql") {
                std::cout << "使用MySQL数据库..." << std::endl;
                dbService_ = &services::MySqlDatabaseService::instance();
                
                // 从配置文件读取数据库连接参数
                auto& configManager = ConfigManager::instance();
                services::DbConfig config;
                config.host = configManager.get<std::string>("database.host", "127.0.0.1");
                config.port = configManager.get<int>("database.port", 3306);
                config.dbname = configManager.get<std::string>("database.db_name", "kama_llm");
                config.user = configManager.get<std::string>("database.user", "root");
                config.password = configManager.get<std::string>("database.password", "password");
                config.charset = configManager.get<std::string>("database.charset", "utf8mb4");
                
                std::cout << "数据库连接参数: " << std::endl
                          << "  主机: " << config.host << std::endl
                          << "  端口: " << config.port << std::endl
                          << "  数据库名: " << config.dbname << std::endl
                          << "  用户名: " << config.user << std::endl
                          << "  字符集: " << config.charset << std::endl;
                
                std::cout << "初始化MySQL数据库连接..." << std::endl;
                if (!dynamic_cast<services::MySqlDatabaseService*>(dbService_)->initialize(config)) {
                    std::cerr << "MySQL数据库服务初始化失败" << std::endl;
                    LOG_ERROR << "数据库服务初始化失败";
                    
                    // 如果MySQL连接失败，回退到内存数据库
                    std::cout << "回退到内存数据库..." << std::endl;
                    auto memoryDb = new services::MemoryDatabaseService();
                    memoryDb->initialize();
                    dbService_ = memoryDb;
                    dbType_ = "memory"; // 更新数据库类型
                    std::cout << "内存数据库初始化成功" << std::endl;
                    LOG_INFO << "回退使用内存数据库服务";
                } else {
                    std::cout << "MySQL数据库初始化成功" << std::endl;
                }
            } else {
                // 使用内存数据库作为替代
                std::cout << "使用内存数据库..." << std::endl;
                auto memoryDb = new services::MemoryDatabaseService();
                memoryDb->initialize();
                dbService_ = memoryDb;
                std::cout << "内存数据库初始化成功" << std::endl;
                LOG_INFO << "使用内存数据库服务";
            }
            
            // 初始化LLaMA服务
            std::cout << "初始化LLaMA服务..." << std::endl;
            std::cout << "LLaMA服务类型: " << llamaServiceType_ << std::endl;
            
            try {
                if (llamaServiceType_ == "tcp") {
                    // 使用TCP连接的LLaMA服务
                    std::cout << "使用TCP连接的LLaMA服务" << std::endl;
                    std::cout << "模型路径: " << llamaModelPath_ << std::endl;
                    std::cout << "服务器IP: " << llamaServerIp_ << std::endl;
                    std::cout << "服务器端口: " << llamaServerPort_ << std::endl;
                    
                    llamaService_ = std::make_unique<services::LlamaTcpService>(
                        llamaModelPath_, llamaServerIp_, llamaServerPort_);
                    std::cout << "LLaMA TCP服务创建成功" << std::endl;
                } else {
                    // 使用模拟LLaMA服务
                    std::cout << "使用模拟LLaMA服务" << std::endl;
                    llamaService_ = std::make_unique<services::LlamaMockService>();
                    std::cout << "LLaMA模拟服务创建成功" << std::endl;
                    LOG_INFO << "使用模拟LLaMA服务";
                }
                
                // 检查LLaMA服务状态
                std::cout << "检查LLaMA服务状态..." << std::endl;
                bool available = llamaService_->isAvailable();
                std::cout << "LLaMA服务状态: " << (available ? "可用" : "不可用") << std::endl;
                
                if (available) {
                    LOG_INFO << "LLaMA服务连接成功";
                } else {
                    LOG_WARN << "LLaMA服务不可用，将使用降级回复";
                }
            } catch (const std::exception& e) {
                std::cerr << "初始化LLaMA服务异常: " << e.what() << std::endl;
                LOG_ERROR << "初始化LLaMA服务异常: " << e.what();
                // 使用模拟服务作为备选
                std::cout << "尝试使用LLaMA模拟服务作为备选..." << std::endl;
                llamaService_ = std::make_unique<services::LlamaMockService>(true);
                std::cout << "LLaMA模拟备选服务创建成功" << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "服务初始化异常: " << e.what() << std::endl;
            LOG_ERROR << "服务初始化异常: " << e.what();
            throw;
        }
    }
    
    // 设置路由
    void setupRoutes() {
        // 主页重定向
        server_.get("/", [](const HttpRequest& req, HttpResponse& resp) {
            resp.setStatusCode(HttpStatusCode::MOVED_PERMANENTLY);
            resp.addHeader("Location", "/index.html");
        });
        
        // 样例页面重定向
        server_.get("/basic", [](const HttpRequest& req, HttpResponse& resp) {
            resp.setStatusCode(HttpStatusCode::MOVED_PERMANENTLY);
            resp.addHeader("Location", "/basic.html");
        });
        
        // LLaMA查询API
        server_.post("/api/llama/query", [this](const HttpRequest& req, HttpResponse& resp) {
            handleLlamaQuery(req, resp);
        });
        
        // 系统状态API
        server_.get("/api/status", [this](const HttpRequest& req, HttpResponse& resp) {
            handleStatusRequest(req, resp);
        });
    }
    
    // 处理LLaMA查询请求
    void handleLlamaQuery(const HttpRequest& req, HttpResponse& resp) {
        std::string body = req.body();
        if (body.empty()) {
            resp.setStatusCode(HttpStatusCode::BAD_REQUEST);
            resp.setContentType("application/json");
            resp.setBody("{\"error\": \"请求体不能为空\"}");
            return;
        }
        
        try {
            json request = json::parse(body);
            
            if (!request.contains("query") || !request["query"].is_string()) {
                resp.setStatusCode(HttpStatusCode::BAD_REQUEST);
                resp.setContentType("application/json");
                resp.setBody("{\"error\": \"请求格式错误，缺少query字段\"}");
                return;
            }
            
            std::string query = request["query"];
            LOG_INFO << "接收到LLaMA查询请求: " << (query.length() > 50 ? query.substr(0, 50) + "..." : query);
            
            // 首先尝试从缓存获取响应
            bool cached = false;
            std::string response;
            
            if (dbService_->isInitialized()) {
                response = dbService_->getResponseFromCache(query);
                if (!response.empty()) {
                    cached = true;
                    LOG_INFO << "从缓存返回响应";
                }
            }
            
            // 如果缓存未命中，查询LLaMA服务
            if (response.empty()) {
                // 检查LLaMA服务是否可用
                if (!llamaService_->isAvailable()) {
                    // 尝试重置连接
                    if (!llamaService_->resetConnection()) {
                        resp.setStatusCode(HttpStatusCode::SERVICE_UNAVAILABLE);
                        resp.setContentType("application/json");
                        resp.setBody("{\"error\": \"LLaMA服务不可用\"}");
                        return;
                    }
                }
                
                try {
                    // 调用LLaMA服务
                    response = llamaService_->query(query);
                    
                    // 保存到缓存
                    if (dbService_->isInitialized() && !response.empty()) {
                        dbService_->saveToCache(query, response);
                    }
                } catch (const std::exception& e) {
                    LOG_ERROR << "调用LLaMA服务异常: " << e.what();
                    resp.setStatusCode(HttpStatusCode::INTERNAL_SERVER_ERROR);
                    resp.setContentType("application/json");
                    resp.setBody("{\"error\": \"处理请求时发生错误\"}");
                    return;
                }
            } else {
                // 更新缓存统计
                dbService_->updateCacheStats(query);
            }
            
            // 构建响应
            json responseJson = {
                {"response", response},
                {"cached", cached}
            };
            
            resp.setStatusCode(HttpStatusCode::OK);
            resp.setContentType("application/json");
            resp.setBody(responseJson.dump());
            
        } catch (const json::parse_error& e) {
            LOG_ERROR << "解析JSON失败: " << e.what();
            resp.setStatusCode(HttpStatusCode::BAD_REQUEST);
            resp.setContentType("application/json");
            resp.setBody("{\"error\": \"无效的JSON格式\"}");
        } catch (const std::exception& e) {
            LOG_ERROR << "处理查询请求异常: " << e.what();
            resp.setStatusCode(HttpStatusCode::INTERNAL_SERVER_ERROR);
            resp.setContentType("application/json");
            resp.setBody("{\"error\": \"服务器内部错误\"}");
        }
    }
    
    // 处理系统状态请求
    void handleStatusRequest(const HttpRequest& req, HttpResponse& resp) {
        try {
            json status;
            
            // 基本状态信息
            status["llama_available"] = llamaService_->isAvailable();
            status["db_available"] = dbService_->isInitialized();
            status["timestamp"] = std::time(nullptr);
            
            // 如果数据库可用，添加更多信息
            if (dbService_->isInitialized()) {
                auto dbStats = dbService_->getSystemStats();
                status["cache_size"] = dbStats["cache_count"];
                status["users_count"] = dbStats["users_count"];
                status["sessions_count"] = dbStats["sessions_count"]; 
                status["conversations_count"] = dbStats["conversations_count"];
                status["db_type"] = dbType_;  // 添加数据库类型信息
            } else {
                status["cache_size"] = 0;
            }
            
            resp.setStatusCode(HttpStatusCode::OK);
            resp.setContentType("application/json");
            resp.setBody(status.dump());
            
        } catch (const std::exception& e) {
            LOG_ERROR << "获取系统状态异常: " << e.what();
            resp.setStatusCode(HttpStatusCode::INTERNAL_SERVER_ERROR);
            resp.setContentType("application/json");
            resp.setBody("{\"error\": \"获取系统状态失败\"}");
        }
    }

private:
    HttpServer server_;
    
    // 配置项
    int port_;
    int threadNum_;
    std::string llamaServiceType_;
    std::string llamaModelPath_;
    std::string llamaServerIp_;
    int llamaServerPort_;
    std::string dbType_;
    
    // 服务组件
    services::IDatabaseService* dbService_;
    std::unique_ptr<services::ILlamaService> llamaService_;
};

// 异步日志
AsyncLogging* g_asyncLog = nullptr;
void asyncOutput(const char* msg, int len) {
    if (g_asyncLog) {
        g_asyncLog->append(msg, len);
    }
}

int main(int argc, char* argv[]) {
    try {
        // 直接输出到控制台，确保能看到启动信息
        std::cout << "开始初始化模块化HTTP服务器..." << std::endl;
        
        // 设置日志
        std::cout << "初始化日志系统..." << std::endl;
        AsyncLogging log("kama_http_server_modular", 1000 * 1000);
        log.start();
        g_asyncLog = &log;
        Logger::setOutput(asyncOutput);
        std::cout << "日志系统初始化完成" << std::endl;
        
        // 创建事件循环
        std::cout << "创建事件循环..." << std::endl;
        EventLoop loop;
        std::cout << "事件循环创建完成" << std::endl;
        
        // 检查端口是否已被占用
        int port = 8080;
        bool usePort = false;
        
        // 检查命令行参数是否指定端口
        for (int i = 1; i < argc; i++) {
            if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
                port = std::atoi(argv[i+1]);
                std::cout << "使用命令行指定端口: " << port << std::endl;
                usePort = true;
                break;
            }
        }
        
        if (!usePort) {
            // 尝试从配置获取端口
            try {
                auto& config = ConfigManager::instance();
                config.initialize();
                port = config.get<int>("server.http_port", 8081); // 使用server.http_port
                std::cout << "使用配置文件指定端口: " << port << std::endl;
            } catch (const std::exception& e) {
                std::cerr << "读取配置端口时出错: " << e.what() << ", 使用默认端口8080" << std::endl;
            }
        }
        
        // 检查端口是否已被占用 - 创建一个临时socket检测
        int sockfd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) {
            std::cerr << "创建检测socket失败: " << strerror(errno) << std::endl;
        } else {
            // 设置SO_REUSEADDR以防止因TIME_WAIT状态的连接导致端口不可用
            int optval = 1;
            ::setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
            
            struct sockaddr_in addr;
            memset(&addr, 0, sizeof(addr));
            addr.sin_family = AF_INET;
            addr.sin_port = htons(port);
            addr.sin_addr.s_addr = INADDR_ANY;
            
            if (::bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
                std::cerr << "警告: 端口 " << port << " 已被占用: " << strerror(errno) << std::endl;
                std::cerr << "服务可能已在运行，或其他程序占用了该端口" << std::endl;
                std::cerr << "请确保端口" << port << "未被占用，或在配置文件中设置其他端口" << std::endl;
                return 1;  // 退出程序
            }
            ::close(sockfd);
        }
        
        // 服务器绑定地址
        std::cout << "设置监听地址..." << std::endl;
        InetAddress listenAddr(port);
        std::cout << "监听地址设置完成: " << port << std::endl;
        
        // 初始化应用
        std::cout << "初始化应用..." << std::endl;
        std::cout << "静态文件根目录: ./static" << std::endl;
        KamaHttpApplication app(&loop, listenAddr, "./static");
        std::cout << "应用初始化完成" << std::endl;
        
        // 启动服务器
        std::cout << "启动服务器..." << std::endl;
        app.start();
        std::cout << "服务器启动完成" << std::endl;
        
        // 运行事件循环
        std::cout << "进入事件循环..." << std::endl;
        loop.loop();
        
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "程序异常: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "未知异常，程序崩溃" << std::endl;
        return 1;
    }
}
