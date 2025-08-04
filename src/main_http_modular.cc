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
#include <thread>
#include <chrono>
#include <arpa/inet.h>
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
            // 重新启用静态文件服务（已优化）
            server_.setStaticFileRoot(staticFilesRoot);
            server_.enableStaticFiles(true);
            std::cout << "静态文件服务配置完成（已优化）" << std::endl;
            
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
            
            std::cout << "读取server.development_mode配置项..." << std::endl;
            developmentMode_ = config.get<bool>("server.development_mode", true);
            std::cout << "server.development_mode = " << developmentMode_ << std::endl;
            
            LOG_INFO << "配置加载完成: " 
                     << "端口=" << port_ 
                     << ", 线程数=" << threadNum_
                     << ", LLaMA服务类型=" << llamaServiceType_
                     << ", 数据库类型=" << dbType_
                     << ", 开发模式=" << developmentMode_;
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
            developmentMode_ = true;  // 默认开启开发模式
            std::cout << "默认配置设置完成" << std::endl;
        }
    }
    
    // 设置中间件
    void setupMiddleware() {
        // 设置开发模式
        server_.setDevelopmentMode(developmentMode_);
        
        // 启用日志中间件
        server_.enableLogging();
        
        // 启用CORS
        server_.enableCors("*");
        
        // 暂时禁用可能导致阻塞的中间件
        // TODO: 调试完成后重新启用
        // server_.enableRateLimit(60, 60);
        // server_.enableCompression();
        // auto authMiddleware = server_.enableAuth();
        
        std::cout << "中间件配置：仅启用日志和CORS" << std::endl;
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
        // 主页现在使用 Open WebUI 风格界面
        // 静态文件处理器会自动处理根路径，返回 index.html
        
        // 样例页面重定向
        server_.get("/basic", [](const HttpRequest& req, HttpResponse& resp) {
            resp.setStatusCode(HttpStatusCode::MOVED_PERMANENTLY);
            resp.addHeader("Location", "/basic.html");
        });
        
        // 模型列表API
        server_.get("/api/models", [this](const HttpRequest& req, HttpResponse& resp) {
            handleModelsRequest(req, resp);
        });
        
        // LLaMA查询API
        server_.post("/api/llama/query", [this](const HttpRequest& req, HttpResponse& resp) {
            handleLlamaQuery(req, resp);
        });
        
        // 系统状态API
        server_.get("/api/status", [this](const HttpRequest& req, HttpResponse& resp) {
            handleStatusRequest(req, resp);
        });
        
        // 清理缓存API
        server_.post("/api/admin/clear-cache", [this](const HttpRequest& req, HttpResponse& resp) {
            handleClearCacheRequest(req, resp);
        });
        
        // 清理数据库API
        server_.post("/api/admin/clear-database", [this](const HttpRequest& req, HttpResponse& resp) {
            handleClearDatabaseRequest(req, resp);
        });
        
        // 数据库统计信息API
        server_.get("/api/admin/database-stats", [this](const HttpRequest& req, HttpResponse& resp) {
            handleDatabaseStatsRequest(req, resp);
        });
        
        // 用户信息API
        server_.get("/api/admin/users", [this](const HttpRequest& req, HttpResponse& resp) {
            handleUsersRequest(req, resp);
        });
        
        // 清除用户注册表API
        server_.post("/api/admin/clear-users", [this](const HttpRequest& req, HttpResponse& resp) {
            handleClearUsersRequest(req, resp);
        });
        
        // 获取对话历史API
        server_.get("/api/conversations", [this](const HttpRequest& req, HttpResponse& resp) {
            handleConversationsRequest(req, resp);
        });
    }
    
    // 处理模型列表请求
    void handleModelsRequest(const HttpRequest& req, HttpResponse& resp) {
        try {
            // 简化的模型列表响应
            std::string modelsJson = R"({
                "success": true,
                "models": [
                    {
                        "id": "llama-7b",
                        "name": "LLaMA 7B",
                        "description": "7B参数的LLaMA模型",
                        "type": "text-generation"
                    }
                ]
            })";
            
            resp.setStatusCode(HttpStatusCode::OK);
            resp.setContentType("application/json");
            resp.enableCORS();
            resp.setBody(modelsJson);
            
        } catch (const std::exception& e) {
            resp.setStatusCode(HttpStatusCode::INTERNAL_SERVER_ERROR);
            resp.setContentType("application/json");
            resp.enableCORS();
            resp.setBody(R"({"success": false, "error": "获取模型列表失败"})");
        }
    }
    
    // 处理清理缓存请求
    void handleClearCacheRequest(const HttpRequest& req, HttpResponse& resp) {
        try {
            if (dbService_ && dbService_->isInitialized()) {
                // 清理缓存
                int clearedCount = dbService_->clearCache();
                
                // 返回结果
                json responseJson = {
                    {"success", true},
                    {"message", "缓存清理成功"},
                    {"cleared_count", clearedCount}
                };
                
                resp.setStatusCode(HttpStatusCode::OK);
                resp.setContentType("application/json");
                resp.setBody(responseJson.dump());
                
                LOG_INFO << "缓存清理完成，共清理 " << clearedCount << " 项";
            } else {
                resp.setStatusCode(HttpStatusCode::SERVICE_UNAVAILABLE);
                resp.setContentType("application/json");
                resp.setBody("{\"error\": \"数据库服务不可用\"}");
                LOG_ERROR << "清理缓存失败：数据库服务不可用";
            }
        } catch (const std::exception& e) {
            LOG_ERROR << "清理缓存异常: " << e.what();
            resp.setStatusCode(HttpStatusCode::INTERNAL_SERVER_ERROR);
            resp.setContentType("application/json");
            resp.setBody("{\"error\": \"清理缓存时发生错误\"}");
        }
    }
    
    // 处理清理数据库请求
    void handleClearDatabaseRequest(const HttpRequest& req, HttpResponse& resp) {
        try {
            if (dbService_ && dbService_->isInitialized()) {
                // 获取要清理的表名列表
                std::vector<std::string> tables;
                
                // 尝试从请求体中解析表名
                try {
                    std::string body = req.body();
                    if (!body.empty()) {
                        json requestJson = json::parse(body);
                        if (requestJson.contains("tables") && requestJson["tables"].is_array()) {
                            for (const auto& table : requestJson["tables"]) {
                                if (table.is_string()) {
                                    tables.push_back(table.get<std::string>());
                                }
                            }
                        }
                    }
                } catch (const json::parse_error& e) {
                    // 解析错误，忽略
                }
                
                // 如果没有指定表，默认清理所有表
                bool success = false;
                std::string message;
                
                if (tables.empty()) {
                    success = dbService_->resetDatabase();
                    message = "数据库已完全重置";
                    LOG_INFO << "重置数据库完成";
                } else {
                    success = dbService_->clearTables(tables);
                    message = "指定表已清理";
                    LOG_INFO << "清理指定表完成: " << tables.size() << " 张表";
                }
                
                // 返回结果
                json responseJson = {
                    {"success", success},
                    {"message", message}
                };
                
                resp.setStatusCode(HttpStatusCode::OK);
                resp.setContentType("application/json");
                resp.setBody(responseJson.dump());
            } else {
                resp.setStatusCode(HttpStatusCode::SERVICE_UNAVAILABLE);
                resp.setContentType("application/json");
                resp.setBody("{\"error\": \"数据库服务不可用\"}");
                LOG_ERROR << "清理数据库失败：数据库服务不可用";
            }
        } catch (const std::exception& e) {
            LOG_ERROR << "清理数据库异常: " << e.what();
            resp.setStatusCode(HttpStatusCode::INTERNAL_SERVER_ERROR);
            resp.setContentType("application/json");
            resp.setBody("{\"error\": \"清理数据库时发生错误\"}");
        }
    }
    
    // 处理数据库统计信息请求
    void handleDatabaseStatsRequest(const HttpRequest& req, HttpResponse& resp) {
        try {
            if (dbService_ && dbService_->isInitialized()) {
                json stats = dbService_->getSystemStats();
                
                resp.setStatusCode(HttpStatusCode::OK);
                resp.setContentType("application/json");
                resp.setBody(stats.dump());
                LOG_INFO << "返回数据库统计信息";
            } else {
                resp.setStatusCode(HttpStatusCode::SERVICE_UNAVAILABLE);
                resp.setContentType("application/json");
                resp.setBody("{\"error\": \"数据库服务不可用\"}");
                LOG_ERROR << "获取数据库统计信息失败：数据库服务不可用";
            }
        } catch (const std::exception& e) {
            LOG_ERROR << "获取数据库统计信息异常: " << e.what();
            resp.setStatusCode(HttpStatusCode::INTERNAL_SERVER_ERROR);
            resp.setContentType("application/json");
            resp.setBody("{\"error\": \"获取数据库统计信息时发生错误\"}");
        }
    }
    
    // 处理用户信息请求
    void handleUsersRequest(const HttpRequest& req, HttpResponse& resp) {
        try {
            if (dbService_ && dbService_->isInitialized()) {
                auto users = dbService_->getAllUsers();
                
                json responseJson = {
                    {"users", json::array()},
                    {"total", users.size()}
                };
                
                for (const auto& user : users) {
                    json userJson = {
                        {"username", user.username},
                        {"email", user.email},
                        {"created_at", user.created_at},
                        {"last_login", user.last_login}
                    };
                    responseJson["users"].push_back(userJson);
                }
                
                resp.setStatusCode(HttpStatusCode::OK);
                resp.setContentType("application/json");
                resp.setBody(responseJson.dump());
                LOG_INFO << "返回用户信息，共 " << users.size() << " 个用户";
            } else {
                resp.setStatusCode(HttpStatusCode::SERVICE_UNAVAILABLE);
                resp.setContentType("application/json");
                resp.setBody("{\"error\": \"数据库服务不可用\"}");
                LOG_ERROR << "获取用户信息失败：数据库服务不可用";
            }
        } catch (const std::exception& e) {
            LOG_ERROR << "获取用户信息异常: " << e.what();
            resp.setStatusCode(HttpStatusCode::INTERNAL_SERVER_ERROR);
            resp.setContentType("application/json");
            resp.setBody("{\"error\": \"获取用户信息时发生错误\"}");
        }
    }
    
    // 处理清除用户注册表请求
    void handleClearUsersRequest(const HttpRequest& req, HttpResponse& resp) {
        try {
            if (dbService_ && dbService_->isInitialized()) {
                // 清除用户相关的表
                std::vector<std::string> userTables = {"users"};
                bool success = dbService_->clearTables(userTables);
                
                json responseJson = {
                    {"success", success},
                    {"message", success ? "用户注册表已清除" : "清除用户注册表失败"}
                };
                
                resp.setStatusCode(HttpStatusCode::OK);
                resp.setContentType("application/json");
                resp.setBody(responseJson.dump());
                
                if (success) {
                    LOG_INFO << "用户注册表清除成功";
                } else {
                    LOG_ERROR << "用户注册表清除失败";
                }
            } else {
                resp.setStatusCode(HttpStatusCode::SERVICE_UNAVAILABLE);
                resp.setContentType("application/json");
                resp.setBody("{\"error\": \"数据库服务不可用\"}");
                LOG_ERROR << "清除用户注册表失败：数据库服务不可用";
            }
        } catch (const std::exception& e) {
            LOG_ERROR << "清除用户注册表异常: " << e.what();
            resp.setStatusCode(HttpStatusCode::INTERNAL_SERVER_ERROR);
            resp.setContentType("application/json");
            resp.setBody("{\"error\": \"清除用户注册表时发生错误\"}");
        }
    }
    
    // 处理对话历史请求
    void handleConversationsRequest(const HttpRequest& req, HttpResponse& resp) {
        try {
            // 目前返回空的对话列表，因为我们没有实现对话历史存储
            // 在实际实现中，这里应该从数据库获取用户的对话历史
            json responseJson = {
                {"success", true},
                {"conversations", json::array()},
                {"total", 0}
            };
            
            resp.setStatusCode(HttpStatusCode::OK);
            resp.setContentType("application/json");
            resp.setBody(responseJson.dump());
            LOG_INFO << "返回对话历史信息";
        } catch (const std::exception& e) {
            LOG_ERROR << "获取对话历史异常: " << e.what();
            resp.setStatusCode(HttpStatusCode::INTERNAL_SERVER_ERROR);
            resp.setContentType("application/json");
            resp.setBody("{\"success\": false, \"error\": \"获取对话历史时发生错误\"}");
        }
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
                    LOG_INFO << "准备调用LLaMA服务，查询: " << query;
                    response = llamaService_->query(query);
                    LOG_INFO << "LLaMA服务调用成功，响应长度: " << response.length();
                    
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
            
            // 在构建响应前检查是否包含无效UTF-8字符
            LOG_INFO << "准备构建JSON响应，response长度: " << response.length();
            LOG_INFO << "response内容预览: " << (response.length() > 100 ? response.substr(0, 100) + "..." : response);
            
            // 检查response是否包含"解析错误"前缀，表示上游服务返回了错误
            bool is_error_response = response.find("解析错误") == 0;
            
            try {
                json responseJson = {
                    {"response", is_error_response ? "LLaMA服务返回无效响应，请重试" : response},
                    {"cached", cached}
                };
                
                // 添加错误标志
                if (is_error_response) {
                    responseJson["error"] = true;
                    responseJson["raw_error"] = response;
                }
                
                LOG_INFO << "JSON对象构建成功，准备序列化";
                std::string jsonString = responseJson.dump();
                LOG_INFO << "JSON序列化成功，长度: " << jsonString.length();
                
                resp.setStatusCode(HttpStatusCode::OK); // 即使有错误也返回200，但在JSON中标记错误
                resp.setContentType("application/json");
                resp.setBody(jsonString);
            } catch (const std::exception& e) {
                // 如果序列化失败，尝试使用安全的方式构建响应
                LOG_ERROR << "构建响应时出错: " << e.what();
                
                // 使用直接字符串构建作为回退方案
                std::string safeResponse = "{\"response\":\"服务处理请求时出错，请重试\",\"cached\":false,\"error\":true}";
                resp.setStatusCode(HttpStatusCode::OK);
                resp.setContentType("application/json");
                resp.setBody(safeResponse);
            }
            
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
            // 恢复完整的状态响应，但使用安全的方式
            std::string statusJson = "{";
            statusJson += "\"llama_available\": ";
            statusJson += (llamaService_ && llamaService_->isAvailable()) ? "true" : "false";
            statusJson += ", \"db_available\": ";
            statusJson += (dbService_ && dbService_->isInitialized()) ? "true" : "false";
            statusJson += ", \"timestamp\": ";
            statusJson += std::to_string(std::time(nullptr));
            statusJson += ", \"db_type\": \"";
            statusJson += dbType_;
            statusJson += "\", \"cache_size\": 0";
            statusJson += "}";
            
            resp.setStatusCode(HttpStatusCode::OK);
            resp.setContentType("application/json");
            resp.setBody(statusJson);
            
        } catch (const std::exception& e) {
            // 简化错误处理
            resp.setStatusCode(HttpStatusCode::INTERNAL_SERVER_ERROR);
            resp.setContentType("application/json");
            resp.setBody(R"({"error": "获取系统状态失败"})");
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
    bool developmentMode_;  // 开发模式标识
    
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
        AsyncLogging log("logs/llama_http_server", 1000 * 1000);
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
        InetAddress listenAddr(port, "0.0.0.0");  // 监听所有地址，允许外部访问
        std::cout << "监听地址设置完成: " << port << " (0.0.0.0)" << std::endl;
        
        // 初始化应用
        std::cout << "初始化应用..." << std::endl;
        std::cout << "静态文件根目录: ./static" << std::endl;
        KamaHttpApplication app(&loop, listenAddr, "./static");
        std::cout << "应用初始化完成" << std::endl;
        
        // 启动服务器
        std::cout << "启动服务器..." << std::endl;
        app.start();
        std::cout << "服务器启动完成" << std::endl;
        
        // 在独立线程中进行健康检查
        std::thread healthCheck([port]() {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            std::cout << "执行健康检查..." << std::endl;
            
            // 简单的socket连接测试
            int sockfd = ::socket(AF_INET, SOCK_STREAM, 0);
            if (sockfd >= 0) {
                struct sockaddr_in addr;
                memset(&addr, 0, sizeof(addr));
                addr.sin_family = AF_INET;
                addr.sin_port = htons(port);
                addr.sin_addr.s_addr = inet_addr("127.0.0.1");
                
                if (::connect(sockfd, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
                    std::cout << "✅ 健康检查通过，服务器响应正常" << std::endl;
                } else {
                    std::cout << "❌ 健康检查失败，服务器无响应" << std::endl;
                }
                ::close(sockfd);
            }
        });
        healthCheck.detach();
        
        // 运行事件循环
        std::cout << "进入事件循环..." << std::endl;
        std::cout << "HTTP服务器已启动，可以接受请求" << std::endl;
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
