#include "LlamaHttpApplication.h"
#include "http/Middleware.h"
#include "services/DatabaseService.h"
#include "services/LlamaService.h"
#include "services/LlamaMockService.h"
#include "ConfigManager.h"
#include "Logger.h"
#include <nlohmann/json.hpp>
#include <iostream>

using namespace llama;
using namespace llama::http;
using json = nlohmann::json;

LlamaHttpApplication::LlamaHttpApplication(
    EventLoop* loop, 
    const InetAddress& addr,
    const std::string& staticFilesRoot
) : 
    server_(loop, addr, "LlamaHttpServer"),
    dbService_(nullptr),
    llamaService_(nullptr),
    startTime_(std::time(nullptr))
{
    try {
        std::cout << "成员变量初始化完成" << std::endl;
        
        std::cout << "配置静态文件服务..." << std::endl;
        server_.setStaticFileRoot(staticFilesRoot);
        server_.enableStaticFiles(true);
        std::cout << "静态文件服务配置完成（已优化）" << std::endl;
        
        std::cout << "读取配置..." << std::endl;
        loadConfig();
        std::cout << "配置读取完成" << std::endl;
        
        std::cout << "设置中间件..." << std::endl;
        setupMiddleware();
        std::cout << "中间件设置完成" << std::endl;
        
        std::cout << "初始化服务..." << std::endl;
        initServices();
        std::cout << "服务初始化完成" << std::endl;
        
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

void LlamaHttpApplication::start() {
    LOG_INFO << "启动Llama HTTP服务器, 端口: " << port_;
    server_.setThreadNum(threadNum_);
    server_.start();
}

void LlamaHttpApplication::loadConfig() {
    try {
        std::cout << "加载ConfigManager..." << std::endl;
        auto& config = ConfigManager::instance();
        
        std::cout << "初始化ConfigManager..." << std::endl;
        config.initialize();
        std::cout << "ConfigManager初始化完成" << std::endl;
        
        std::cout << "读取http.port配置项..." << std::endl;
        port_ = config.get<int>("server.http_port", 8081);
        std::cout << "http.port = " << port_ << std::endl;
        
        std::cout << "读取http.threads配置项..." << std::endl;
        threadNum_ = config.get<int>("http.threads", 4);
        std::cout << "http.threads = " << threadNum_ << std::endl;
        
        std::cout << "读取llama.service_type配置项..." << std::endl;
        llamaServiceType_ = config.get<std::string>("llama.service_type", "tcp");
        std::cout << "llama.service_type = " << llamaServiceType_ << std::endl;
        
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
        dbType_ = config.get<std::string>("db.type", "memory");
        std::cout << "db.type = " << dbType_ << std::endl;
        
        std::cout << "读取server.development_mode配置项..." << std::endl;
        developmentMode_ = config.get<bool>("server.development_mode", true);
        std::cout << "server.development_mode = " << developmentMode_ << std::endl;
        
        LOG_INFO << "配置加载完成: 开发模式=" << developmentMode_;
    } catch (const std::exception& e) {
        std::cerr << "加载配置异常: " << e.what() << std::endl;
        LOG_ERROR << "加载配置失败: " << e.what() << "，使用默认配置";
        
        std::cout << "使用默认配置..." << std::endl;
        port_ = 8080;
        threadNum_ = 4;
        llamaServiceType_ = "tcp";
        llamaModelPath_ = "/home/shl203/llama.cpp/models/qwen/Qwen-7B-Chat.Q4_K_M.gguf";
        llamaServerIp_ = "127.0.0.1";
        llamaServerPort_ = 8899;
        dbType_ = "memory";
        developmentMode_ = true;
        std::cout << "默认配置设置完成" << std::endl;
    }
}

void LlamaHttpApplication::setupMiddleware() {
    server_.setDevelopmentMode(developmentMode_);
    server_.enableLogging();
    server_.enableCors("*");
    std::cout << "中间件配置：仅启用日志和CORS" << std::endl;
}

void LlamaHttpApplication::initServices() {
    try {
        std::cout << "初始化数据库服务..." << std::endl;
        std::cout << "数据库类型: " << dbType_ << std::endl;
        
        if (dbType_ == "mysql") {
            std::cout << "初始化MySQL数据库服务..." << std::endl;
            dbService_ = &llama::services::MySqlDatabaseService::instance();
            
            // 获取配置管理器实例
            auto& config = ConfigManager::instance();
            
            // 构造数据库配置
            llama::services::DbConfig dbConfig;
            dbConfig.host = config.get<std::string>("database.host", "127.0.0.1");
            dbConfig.port = config.get<int>("database.port", 3306);
            dbConfig.dbname = config.get<std::string>("database.db_name", "llama_llm");
            dbConfig.user = config.get<std::string>("database.user", "root");
            dbConfig.password = config.get<std::string>("database.password", "password");
            dbConfig.charset = config.get<std::string>("database.charset", "utf8mb4");
            
            // 初始化数据库连接
            auto* mysqlService = dynamic_cast<llama::services::MySqlDatabaseService*>(dbService_);
            if (mysqlService && !mysqlService->initialize(dbConfig)) {
                std::cerr << "MySQL数据库初始化失败，降级使用内存数据库" << std::endl;
                dbType_ = "memory";
            } else {
                std::cout << "MySQL数据库服务初始化完成" << std::endl;
            }
        } else {
            std::cout << "初始化内存数据库服务..." << std::endl;
            dbService_ = &llama::services::MySqlDatabaseService::instance();
            std::cout << "内存数据库服务初始化完成" << std::endl;
        }
        
        std::cout << "初始化LLaMA服务..." << std::endl;
        std::cout << "LLaMA服务类型: " << llamaServiceType_ << std::endl;
        
        try {
            if (llamaServiceType_ == "mock") {
                std::cout << "使用Mock LLaMA服务..." << std::endl;
                llamaService_ = std::make_unique<llama::services::LlamaMockService>();
            } else {
                std::cout << "使用TCP LLaMA服务..." << std::endl;
                llamaService_ = std::make_unique<llama::services::LlamaTcpService>("", llamaServerIp_, llamaServerPort_);
            }
            std::cout << "LLaMA服务初始化完成" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "LLaMA服务初始化失败: " << e.what() << std::endl;
            std::cout << "降级使用Mock服务..." << std::endl;
            llamaService_ = std::make_unique<llama::services::LlamaMockService>();
        }
    } catch (const std::exception& e) {
        std::cerr << "服务初始化异常: " << e.what() << std::endl;
        LOG_ERROR << "服务初始化异常: " << e.what();
        throw;
    }
}

void LlamaHttpApplication::setupRoutes() {
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
    
    // 查看缓存内容API
    server_.get("/api/admin/cache-content", [this](const HttpRequest& req, HttpResponse& resp) {
        handleCacheContentRequest(req, resp);
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
    
    // 删除指定用户API
    server_.post("/api/admin/delete-user", [this](const HttpRequest& req, HttpResponse& resp) {
        handleDeleteUserRequest(req, resp);
    });
    
    // 用户注册API
    server_.post("/api/auth/register", [this](const HttpRequest& req, HttpResponse& resp) {
        handleUserRegisterRequest(req, resp);
    });
    
    // 用户登录API
    server_.post("/api/auth/login", [this](const HttpRequest& req, HttpResponse& resp) {
        handleUserLoginRequest(req, resp);
    });
    
    // 用户注销API
    server_.post("/api/auth/logout", [this](const HttpRequest& req, HttpResponse& resp) {
        handleUserLogoutRequest(req, resp);
    });
    
    // 获取对话历史API
    server_.get("/api/conversations", [this](const HttpRequest& req, HttpResponse& resp) {
        handleConversationsRequest(req, resp);
    });
}

std::string LlamaHttpApplication::formatTimestamp(time_t timestamp) {
    if (timestamp <= 0) {
        return "未知";
    }
    
    char timeStr[32];
    struct tm* tm_info = localtime(&timestamp);
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", tm_info);
    return std::string(timeStr);
}

// 处理模型列表请求
void LlamaHttpApplication::handleModelsRequest(const HttpRequest& req, HttpResponse& resp) {
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

// 处理LLaMA查询请求
void LlamaHttpApplication::handleLlamaQuery(const HttpRequest& req, HttpResponse& resp) {
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
            if (!llamaService_ || !llamaService_->isAvailable()) {
                // 尝试重置连接
                if (!llamaService_ || !llamaService_->resetConnection()) {
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
        
        // 构建响应
        try {
            json responseJson = {
                {"response", response},
                {"cached", cached}
            };
            
            resp.setStatusCode(HttpStatusCode::OK);
            resp.setContentType("application/json");
            resp.setBody(responseJson.dump());
        } catch (const std::exception& e) {
            LOG_ERROR << "构建响应时出错: " << e.what();
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
void LlamaHttpApplication::handleStatusRequest(const HttpRequest& req, HttpResponse& resp) {
    try {
        json stats = {
            {"llama_available", llamaService_ && llamaService_->isAvailable()},
            {"db_available", dbService_ && dbService_->isInitialized()},
            {"timestamp", startTime_},
            {"db_type", dbType_},
            {"cache_size", 0}
        };
        
        resp.setStatusCode(HttpStatusCode::OK);
        resp.setContentType("application/json");
        resp.setBody(stats.dump());
        
    } catch (const std::exception& e) {
        resp.setStatusCode(HttpStatusCode::INTERNAL_SERVER_ERROR);
        resp.setContentType("application/json");
        resp.setBody(R"({"error": "获取系统状态失败"})");
    }
}

// 处理清理缓存请求
void LlamaHttpApplication::handleClearCacheRequest(const HttpRequest& req, HttpResponse& resp) {
    try {
        if (dbService_ && dbService_->isInitialized()) {
            int clearedCount = dbService_->clearCache();
            
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

// 处理查看缓存内容请求
void LlamaHttpApplication::handleCacheContentRequest(const HttpRequest& req, HttpResponse& resp) {
    try {
        if (dbService_ && dbService_->isInitialized()) {
            auto cacheData = dbService_->getCacheStats(50);
            
            json responseJson = {
                {"success", true},
                {"cache_count", cacheData.size()},
                {"cache_items", json::array()}
            };
            
            for (const auto& item : cacheData) {
                std::string trimmed_response = item.response_text;
                if (trimmed_response.length() > 200) {
                    size_t cutoff = 200;
                    while (cutoff > 0 && (trimmed_response[cutoff] & 0x80) && !(trimmed_response[cutoff] & 0x40)) {
                        cutoff--;
                    }
                    trimmed_response = trimmed_response.substr(0, cutoff) + "...";
                }
                
                json cacheItem = {
                    {"query", item.query_text},
                    {"response", trimmed_response},
                    {"timestamp", item.created_at},
                    {"access_count", item.hit_count},
                    {"last_accessed", item.last_accessed}
                };
                responseJson["cache_items"].push_back(cacheItem);
            }
            
            resp.setStatusCode(HttpStatusCode::OK);
            resp.setContentType("application/json");
            resp.setBody(responseJson.dump());
            
            LOG_INFO << "返回缓存内容，共 " << cacheData.size() << " 项";
        } else {
            resp.setStatusCode(HttpStatusCode::SERVICE_UNAVAILABLE);
            resp.setContentType("application/json");
            resp.setBody("{\"error\": \"数据库服务不可用\"}");
            LOG_ERROR << "获取缓存内容失败：数据库服务不可用";
        }
    } catch (const std::exception& e) {
        LOG_ERROR << "获取缓存内容异常: " << e.what();
        resp.setStatusCode(HttpStatusCode::INTERNAL_SERVER_ERROR);
        resp.setContentType("application/json");
        resp.setBody("{\"error\": \"获取缓存内容时发生错误\"}");
    }
}

// 处理清理数据库请求
void LlamaHttpApplication::handleClearDatabaseRequest(const HttpRequest& req, HttpResponse& resp) {
    try {
        if (dbService_ && dbService_->isInitialized()) {
            std::vector<std::string> tables;
            
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
void LlamaHttpApplication::handleDatabaseStatsRequest(const HttpRequest& req, HttpResponse& resp) {
    try {
        if (dbService_ && dbService_->isInitialized()) {
            json stats = {
                {"status", "connected"},
                {"type", dbType_},
                {"uptime_seconds", std::time(nullptr) - startTime_},
                {"server_status", "running"}
            };
            
            if (dbType_ == "memory") {
                try {
                    json detailedStats = dbService_->getSystemStats();
                    stats.merge_patch(detailedStats);
                } catch (const std::exception& e) {
                    LOG_WARN << "获取详细统计信息失败: " << e.what();
                }
            }
            
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
void LlamaHttpApplication::handleUsersRequest(const HttpRequest& req, HttpResponse& resp) {
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
                    {"created_at", formatTimestamp(user.created_at)},
                    {"last_login", formatTimestamp(user.last_login)}
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
void LlamaHttpApplication::handleClearUsersRequest(const HttpRequest& req, HttpResponse& resp) {
    try {
        if (dbService_ && dbService_->isInitialized()) {
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

// 处理删除指定用户请求
void LlamaHttpApplication::handleDeleteUserRequest(const HttpRequest& req, HttpResponse& resp) {
    try {
        auto bodyStr = req.body();
        if (bodyStr.empty()) {
            resp.setStatusCode(HttpStatusCode::BAD_REQUEST);
            resp.setContentType("application/json");
            resp.setBody("{\"error\": \"请求体不能为空\", \"success\": false}");
            return;
        }
        
        json requestData;
        try {
            requestData = json::parse(bodyStr);
        } catch (const json::parse_error& e) {
            resp.setStatusCode(HttpStatusCode::BAD_REQUEST);
            resp.setContentType("application/json");
            resp.setBody("{\"error\": \"无效的JSON格式\", \"success\": false}");
            return;
        }
        
        if (!requestData.contains("username")) {
            resp.setStatusCode(HttpStatusCode::BAD_REQUEST);
            resp.setContentType("application/json");
            resp.setBody("{\"error\": \"缺少必需字段: username\", \"success\": false}");
            return;
        }
        
        std::string username = requestData["username"];
        
        if (dbService_ && dbService_->isInitialized()) {
            auto userInfo = dbService_->getUserInfo(username);
            if (userInfo.username.empty()) {
                resp.setStatusCode(HttpStatusCode::NOT_FOUND);
                resp.setContentType("application/json");
                resp.setBody("{\"error\": \"用户不存在\", \"success\": false}");
                return;
            }
            
            bool success = dbService_->deleteUser(username);
            
            json responseJson = {
                {"success", success},
                {"message", success ? "用户删除成功" : "用户删除失败"},
                {"username", username}
            };
            
            resp.setStatusCode(HttpStatusCode::OK);
            resp.setContentType("application/json");
            resp.setBody(responseJson.dump());
            
            if (success) {
                LOG_INFO << "用户删除成功: " << username;
            } else {
                LOG_ERROR << "用户删除失败: " << username;
            }
        } else {
            resp.setStatusCode(HttpStatusCode::SERVICE_UNAVAILABLE);
            resp.setContentType("application/json");
            resp.setBody("{\"error\": \"数据库服务不可用\", \"success\": false}");
            LOG_ERROR << "删除用户失败：数据库服务不可用";
        }
    } catch (const std::exception& e) {
        LOG_ERROR << "删除用户异常: " << e.what();
        resp.setStatusCode(HttpStatusCode::INTERNAL_SERVER_ERROR);
        resp.setContentType("application/json");
        resp.setBody("{\"error\": \"删除用户时发生错误\", \"success\": false}");
    }
}

// 处理用户注册请求
void LlamaHttpApplication::handleUserRegisterRequest(const HttpRequest& req, HttpResponse& resp) {
    try {
        auto bodyStr = req.body();
        if (bodyStr.empty()) {
            resp.setStatusCode(HttpStatusCode::BAD_REQUEST);
            resp.setContentType("application/json");
            resp.setBody("{\"error\": \"请求体不能为空\", \"success\": false}");
            return;
        }
        
        json requestData;
        try {
            requestData = json::parse(bodyStr);
        } catch (const json::parse_error& e) {
            resp.setStatusCode(HttpStatusCode::BAD_REQUEST);
            resp.setContentType("application/json");
            resp.setBody("{\"error\": \"无效的JSON格式\", \"success\": false}");
            return;
        }
        
        if (!requestData.contains("username") || !requestData.contains("password") || !requestData.contains("email")) {
            resp.setStatusCode(HttpStatusCode::BAD_REQUEST);
            resp.setContentType("application/json");
            resp.setBody("{\"error\": \"缺少必需字段: username, password, email\", \"success\": false}");
            return;
        }
        
        std::string username = requestData["username"];
        std::string password = requestData["password"];
        std::string email = requestData["email"];
        
        if (username.length() < 3 || username.length() > 50) {
            resp.setStatusCode(HttpStatusCode::BAD_REQUEST);
            resp.setContentType("application/json");
            resp.setBody("{\"error\": \"用户名长度必须在3-50字符之间\", \"success\": false}");
            return;
        }
        
        if (password.length() < 6) {
            resp.setStatusCode(HttpStatusCode::BAD_REQUEST);
            resp.setContentType("application/json");
            resp.setBody("{\"error\": \"密码长度至少6个字符\", \"success\": false}");
            return;
        }
        
        if (dbService_) {
            auto existingUser = dbService_->getUserInfo(username);
            if (!existingUser.username.empty()) {
                resp.setStatusCode(HttpStatusCode::CONFLICT);
                resp.setContentType("application/json");
                resp.setBody("{\"error\": \"用户名已存在\", \"success\": false}");
                return;
            }
            
            bool success = dbService_->createUser(username, password, email);
            if (success) {
                json responseJson = {
                    {"success", true},
                    {"message", "用户注册成功"},
                    {"username", username}
                };
                
                resp.setStatusCode(HttpStatusCode::CREATED);
                resp.setContentType("application/json");
                resp.setBody(responseJson.dump());
                LOG_INFO << "用户注册成功: " << username;
            } else {
                resp.setStatusCode(HttpStatusCode::INTERNAL_SERVER_ERROR);
                resp.setContentType("application/json");
                resp.setBody("{\"error\": \"注册失败，请稍后重试\", \"success\": false}");
                LOG_ERROR << "用户注册失败: " << username;
            }
        } else {
            resp.setStatusCode(HttpStatusCode::SERVICE_UNAVAILABLE);
            resp.setContentType("application/json");
            resp.setBody("{\"error\": \"数据库服务不可用\", \"success\": false}");
            LOG_ERROR << "注册失败：数据库服务不可用";
        }
    } catch (const std::exception& e) {
        LOG_ERROR << "用户注册异常: " << e.what();
        resp.setStatusCode(HttpStatusCode::INTERNAL_SERVER_ERROR);
        resp.setContentType("application/json");
        resp.setBody("{\"error\": \"注册时发生错误\", \"success\": false}");
    }
}

// 处理用户登录请求
void LlamaHttpApplication::handleUserLoginRequest(const HttpRequest& req, HttpResponse& resp) {
    try {
        auto bodyStr = req.body();
        if (bodyStr.empty()) {
            resp.setStatusCode(HttpStatusCode::BAD_REQUEST);
            resp.setContentType("application/json");
            resp.setBody("{\"error\": \"请求体不能为空\", \"success\": false}");
            return;
        }
        
        json requestData;
        try {
            requestData = json::parse(bodyStr);
        } catch (const json::parse_error& e) {
            resp.setStatusCode(HttpStatusCode::BAD_REQUEST);
            resp.setContentType("application/json");
            resp.setBody("{\"error\": \"无效的JSON格式\", \"success\": false}");
            return;
        }
        
        if (!requestData.contains("username") || !requestData.contains("password")) {
            resp.setStatusCode(HttpStatusCode::BAD_REQUEST);
            resp.setContentType("application/json");
            resp.setBody("{\"error\": \"缺少必需字段: username, password\", \"success\": false}");
            return;
        }
        
        std::string username = requestData["username"];
        std::string password = requestData["password"];
        
        if (dbService_) {
            bool authenticated = dbService_->authenticateUser(username, password);
            if (authenticated) {
                dbService_->updateUserLastLogin(username);
                
                auto userInfo = dbService_->getUserInfo(username);
                
                json responseJson = {
                    {"success", true},
                    {"message", "登录成功"},
                    {"user", {
                        {"username", userInfo.username},
                        {"email", userInfo.email},
                        {"user_id", userInfo.user_id}
                    }}
                };
                
                resp.setStatusCode(HttpStatusCode::OK);
                resp.setContentType("application/json");
                resp.setBody(responseJson.dump());
                LOG_INFO << "用户登录成功: " << username;
            } else {
                resp.setStatusCode(HttpStatusCode::UNAUTHORIZED);
                resp.setContentType("application/json");
                resp.setBody("{\"error\": \"用户名或密码错误\", \"success\": false}");
                LOG_WARN << "登录失败，用户名或密码错误: " << username;
            }
        } else {
            resp.setStatusCode(HttpStatusCode::SERVICE_UNAVAILABLE);
            resp.setContentType("application/json");
            resp.setBody("{\"error\": \"数据库服务不可用\", \"success\": false}");
            LOG_ERROR << "登录失败：数据库服务不可用";
        }
    } catch (const std::exception& e) {
        LOG_ERROR << "用户登录异常: " << e.what();
        resp.setStatusCode(HttpStatusCode::INTERNAL_SERVER_ERROR);
        resp.setContentType("application/json");
        resp.setBody("{\"error\": \"登录时发生错误\", \"success\": false}");
    }
}

// 处理用户注销请求
void LlamaHttpApplication::handleUserLogoutRequest(const HttpRequest& req, HttpResponse& resp) {
    try {
        json responseJson = {
            {"success", true},
            {"message", "注销成功"}
        };
        
        resp.setStatusCode(HttpStatusCode::OK);
        resp.setContentType("application/json");
        resp.setBody(responseJson.dump());
        LOG_INFO << "用户注销成功";
    } catch (const std::exception& e) {
        LOG_ERROR << "用户注销异常: " << e.what();
        resp.setStatusCode(HttpStatusCode::INTERNAL_SERVER_ERROR);
        resp.setContentType("application/json");
        resp.setBody("{\"error\": \"注销时发生错误\", \"success\": false}");
    }
}

// 处理对话历史请求
void LlamaHttpApplication::handleConversationsRequest(const HttpRequest& req, HttpResponse& resp) {
    try {
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
