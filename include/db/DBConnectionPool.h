#ifndef KAMA_DB_CONNECTION_POOL_H
#define KAMA_DB_CONNECTION_POOL_H

#include <mysql/mysql.h>
#include <string>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <vector>
#include <atomic>
#include <chrono>
#include <functional>
#include <thread>

namespace kama {
namespace db {

/**
 * 数据库连接包装类，用于封装MYSQL连接对象
 */
class DBConnection {
public:
    DBConnection(const std::string& host, const std::string& user, 
                 const std::string& password, const std::string& dbName, 
                 int port);
    ~DBConnection();

    bool connect();
    bool reconnect();
    bool ping();
    bool execute(const std::string& sql);
    bool query(const std::string& sql, std::function<void(MYSQL_ROW)> rowHandler);
    MYSQL* getRawConnection() { return m_conn; }
    
    // 获取上次错误
    std::string getLastError() const;
    unsigned int getLastErrno() const;

private:
    MYSQL* m_conn;
    std::string m_host;
    std::string m_user;
    std::string m_password;
    std::string m_dbName;
    int m_port;
    std::atomic<bool> m_connected;
};

/**
 * 数据库连接池类
 * 实现数据库连接的创建、管理和复用
 */
class DBConnectionPool {
public:
    // 获取单例
    static DBConnectionPool& getInstance();

    // 初始化连接池
    bool init(const std::string& host, const std::string& user, 
              const std::string& password, const std::string& dbName, 
              int port, int maxSize = 10, int minSize = 5);
    
    // 获取连接
    std::shared_ptr<DBConnection> getConnection(int timeoutMs = 1000);
    
    // 释放连接
    void releaseConnection(std::shared_ptr<DBConnection> conn);
    
    // 获取连接池状态
    size_t getActiveConnectionCount() const;
    size_t getIdleConnectionCount() const;
    size_t getTotalConnectionCount() const;
    
    // 关闭连接池
    void close();

private:
    // 私有构造函数，保证单例模式
    DBConnectionPool();
    ~DBConnectionPool();
    
    // 创建新的连接
    std::shared_ptr<DBConnection> createConnection();
    
    // 保活线程，定期检查连接是否有效
    void keepAliveThreadFunc();

private:
    std::string m_host;
    std::string m_user;
    std::string m_password;
    std::string m_dbName;
    int m_port;
    int m_maxSize;
    int m_minSize;
    
    std::atomic<bool> m_running;
    std::atomic<size_t> m_activeConnCount;
    std::atomic<size_t> m_totalConnCount;
    
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::queue<std::shared_ptr<DBConnection>> m_connectionQueue;
    
    std::thread m_keepAliveThread;
};

} // namespace db
} // namespace kama

#endif // KAMA_DB_CONNECTION_POOL_H
