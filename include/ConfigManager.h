#pragma once

#include <string>
#include <memory>
#include <mutex>
#include "nlohmann/json.hpp"

namespace kama {

class ConfigManager {
public:
    // 单例模式
    static ConfigManager& instance();
    
    // 初始化配置，从文件加载
    bool initialize(const std::string& configPath = "config/config.json");
    
    // 获取配置值（支持路径访问，如 "database.host"）
    template<typename T>
    T get(const std::string& path, const T& defaultValue = T()) const;
    
    // 热重载配置
    bool reload();
    
    // 获取整个配置对象
    nlohmann::json getConfig() const;
    
    // 获取配置对象的引用（允许修改）
    nlohmann::json& getConfigRef() { return config_; }
    
private:
    ConfigManager() = default;
    ~ConfigManager() = default;
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;
    
    // 从路径获取嵌套值
    nlohmann::json getValueFromPath(const std::string& path) const;
    
    nlohmann::json config_;
    std::string configPath_;
    mutable std::mutex mutex_;
    bool initialized_ = false;
};

// 模板方法实现
template<typename T>
T ConfigManager::get(const std::string& path, const T& defaultValue) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) {
        return defaultValue;
    }
    
    try {
        auto value = getValueFromPath(path);
        return value.get<T>();
    } catch (...) {
        return defaultValue;
    }
}

} // namespace kama
