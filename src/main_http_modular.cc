#include "http/HttpServer.h"
#include "http/Middleware.h"
#include "services/IDatabaseService.h"
#include "services/DatabaseService.h"
#include "services/ILlamaService.h"
#include "services/LlamaService.h"
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
        // 配置静态文件服务
        server_.setStaticFileRoot(staticFilesRoot);
        server_.enableStaticFiles(true);
        
        // 读取配置
        loadConfig();
        
        // 添加中间件
        setupMiddleware();
        
        // 初始化服务
        initServices();
        
        // 注册路由
        setupRoutes();
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
            // 这里应该从配置文件读取，现在先硬编码
            port_ = 8080;
            threadNum_ = 4;
            llamaServiceType_ = "tcp";
            llamaModelPath_ = "/path/to/model";
            llamaServerIp_ = "127.0.0.1"; 
            llamaServerPort_ = 8899;
            dbType_ = "mysql";
        } catch (const std::exception& e) {
            LOG_ERROR << "加载配置失败: " << e.what();
            exit(1);
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
        // 初始化数据库服务
        if (dbType_ == "mysql") {
            dbService_ = &services::MySqlDatabaseService::instance();
            
            services::DbConfig config;
            config.host = "localhost"; // 从配置读取
            config.dbname = "kama";
            config.user = "root";
            config.password = "";
            
            if (!dynamic_cast<services::MySqlDatabaseService*>(dbService_)->initialize(config)) {
                LOG_ERROR << "数据库服务初始化失败";
            }
        } else {
            // 使用内存数据库作为替代
            auto memoryDb = new services::MemoryDatabaseService();
            memoryDb->initialize();
            dbService_ = memoryDb;
            LOG_INFO << "使用内存数据库服务";
        }
        
        // 初始化LLaMA服务
        if (llamaServiceType_ == "tcp") {
            // 使用TCP连接的LLaMA服务
            llamaService_ = std::make_unique<services::LlamaTcpService>(
                llamaModelPath_, llamaServerIp_, llamaServerPort_);
        } else {
            // 使用模拟LLaMA服务
            llamaService_ = std::make_unique<services::LlamaMockService>();
            LOG_INFO << "使用模拟LLaMA服务";
        }
        
        // 检查LLaMA服务状态
        if (llamaService_->isAvailable()) {
            LOG_INFO << "LLaMA服务连接成功";
        } else {
            LOG_WARN << "LLaMA服务不可用，将使用降级回复";
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
            status["timestamp"] = std::time(nullptr);
            
            // 如果数据库可用，添加更多信息
            if (dbService_->isInitialized()) {
                auto dbStats = dbService_->getSystemStats();
                status["cache_size"] = dbStats["cache_count"];
                status["users_count"] = dbStats["users_count"];
                status["sessions_count"] = dbStats["sessions_count"]; 
                status["conversations_count"] = dbStats["conversations_count"];
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
    // 设置日志
    AsyncLogging log("kama_http_server", 1000 * 1000);
    log.start();
    g_asyncLog = &log;
    Logger::setOutput(asyncOutput);
    
    // 创建事件循环
    EventLoop loop;
    
    // 服务器绑定地址
    InetAddress listenAddr(8080);
    
    // 初始化应用
    KamaHttpApplication app(&loop, listenAddr, "./static");
    
    // 启动服务器
    app.start();
    
    // 运行事件循环
    loop.loop();
    
    return 0;
}
