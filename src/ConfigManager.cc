#include "ConfigManager.h"
#include "Logger.h"
#include <fstream>
#include <iostream>

namespace llama {

ConfigManager& ConfigManager::instance() {
    static ConfigManager instance;
    return instance;
}

bool ConfigManager::initialize(const std::string& configPath) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    configPath_ = configPath;
    
    try {
        std::ifstream file(configPath_);
        if (!file.is_open()) {
            LOG_ERROR << "无法打开配置文件: " << configPath_;
            return false;
        }
        
        file >> config_;
        initialized_ = true;
        
        LOG_INFO << "成功加载配置文件: " << configPath_;
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR << "解析配置文件失败: " << e.what();
        return false;
    }
}

bool ConfigManager::reload() {
    return initialize(configPath_);
}

nlohmann::json ConfigManager::getConfig() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return config_;
}

nlohmann::json ConfigManager::getValueFromPath(const std::string& path) const {
    std::string::size_type start = 0;
    std::string::size_type end = 0;
    
    nlohmann::json current = config_;
    
    while ((end = path.find('.', start)) != std::string::npos) {
        std::string key = path.substr(start, end - start);
        if (!current.contains(key)) {
            throw std::runtime_error("配置键不存在: " + key);
        }
        current = current[key];
        start = end + 1;
    }
    
    std::string key = path.substr(start);
    if (!current.contains(key)) {
        throw std::runtime_error("配置键不存在: " + key);
    }
    
    return current[key];
}

} // namespace llama
