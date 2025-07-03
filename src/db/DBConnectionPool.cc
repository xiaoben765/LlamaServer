#include "db/DBConnectionPool.h"
#include <iostream>
#include <thread>

namespace kama {
namespace db {

// DBConnection实现
DBConnection::DBConnection(const std::string& host, const std::string& user, 
                          const std::string& password, const std::string& dbName, 
                          int port)
    : m_host(host), m_user(user), m_password(password), m_dbName(dbName),
      m_port(port), m_connected(false) {
    m_conn = mysql_init(nullptr);
}

DBConnection::~DBConnection() {
    if (m_conn) {
        mysql_close(m_conn);
        m_conn = nullptr;
    }
}

bool DBConnection::connect() {
    if (!m_conn) {
        m_conn = mysql_init(nullptr);
        if (!m_conn) {
            return false;
        }
    }

    // 设置自动重连选项
    bool reconnect = 1;
    mysql_options(m_conn, MYSQL_OPT_RECONNECT, &reconnect);
    
    // 设置连接超时
    int timeout = 3;
    mysql_options(m_conn, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);

    if (!mysql_real_connect(m_conn, m_host.c_str(), m_user.c_str(), 
                           m_password.c_str(), m_dbName.c_str(), 
                           m_port, nullptr, 0)) {
        return false;
    }

    m_connected = true;
    return true;
}

bool DBConnection::reconnect() {
    if (m_conn) {
        mysql_close(m_conn);
        m_conn = nullptr;
    }
    
    return connect();
}

bool DBConnection::ping() {
    if (!m_conn) return false;
    
    int ret = mysql_ping(m_conn);
    m_connected = (ret == 0);
    return m_connected;
}

bool DBConnection::execute(const std::string& sql) {
    if (!m_conn || !m_connected) {
        if (!reconnect()) {
            return false;
        }
    }
    
    int ret = mysql_query(m_conn, sql.c_str());
    return (ret == 0);
}

bool DBConnection::query(const std::string& sql, std::function<void(MYSQL_ROW)> rowHandler) {
    if (!m_conn || !m_connected) {
        if (!reconnect()) {
            return false;
        }
    }
    
    if (mysql_query(m_conn, sql.c_str()) != 0) {
        return false;
    }
    
    MYSQL_RES* result = mysql_store_result(m_conn);
    if (!result) {
        return false;
    }
    
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(result))) {
        rowHandler(row);
    }
    
    mysql_free_result(result);
    return true;
}

std::string DBConnection::getLastError() const {
    if (!m_conn) return "Connection not initialized";
    return mysql_error(m_conn);
}

unsigned int DBConnection::getLastErrno() const {
    if (!m_conn) return 0;
    return mysql_errno(m_conn);
}

// DBConnectionPool实现
DBConnectionPool::DBConnectionPool() 
    : m_maxSize(10), m_minSize(5), m_running(false), 
      m_activeConnCount(0), m_totalConnCount(0) {
}

DBConnectionPool::~DBConnectionPool() {
    close();
}

DBConnectionPool& DBConnectionPool::getInstance() {
    static DBConnectionPool instance;
    return instance;
}

bool DBConnectionPool::init(const std::string& host, const std::string& user, 
                           const std::string& password, const std::string& dbName, 
                           int port, int maxSize, int minSize) {
    if (m_running) {
        return true;
    }
    
    std::lock_guard<std::mutex> lock(m_mutex);
    
    m_host = host;
    m_user = user;
    m_password = password;
    m_dbName = dbName;
    m_port = port;
    m_maxSize = maxSize;
    m_minSize = minSize;
    
    // 创建初始连接
    for (int i = 0; i < minSize; ++i) {
        auto conn = createConnection();
        if (conn) {
            m_connectionQueue.push(conn);
            m_totalConnCount++;
        } else {
            // 如果无法创建连接，则返回失败
            std::cerr << "Failed to create initial database connection" << std::endl;
            close();
            return false;
        }
    }
    
    m_running = true;
    
    // 启动保活线程
    m_keepAliveThread = std::thread(&DBConnectionPool::keepAliveThreadFunc, this);
    
    return true;
}

std::shared_ptr<DBConnection> DBConnectionPool::getConnection(int timeoutMs) {
    std::unique_lock<std::mutex> lock(m_mutex);
    
    if (!m_running) {
        return nullptr;
    }
    
    // 等待可用连接或超时
    bool hasConnection = true;
    if (m_connectionQueue.empty()) {
        if (m_totalConnCount < m_maxSize) {
            // 如果还没达到最大连接数，则创建新连接
            auto conn = createConnection();
            if (conn) {
                m_totalConnCount++;
                m_activeConnCount++;
                return conn;
            }
        }
        
        // 等待有连接可用或超时
        hasConnection = m_cv.wait_for(lock, std::chrono::milliseconds(timeoutMs),
            [this] { return !m_connectionQueue.empty() || !m_running; });
        
        if (!m_running) {
            return nullptr;
        }
        
        if (!hasConnection) {
            // 超时
            std::cerr << "Timeout waiting for database connection" << std::endl;
            return nullptr;
        }
    }
    
    // 从队列取出一个连接
    auto conn = m_connectionQueue.front();
    m_connectionQueue.pop();
    m_activeConnCount++;
    
    lock.unlock();
    
    // 检查连接是否有效，无效则重连
    if (!conn->ping()) {
        if (!conn->reconnect()) {
            m_activeConnCount--;
            return nullptr;
        }
    }
    
    return conn;
}

void DBConnectionPool::releaseConnection(std::shared_ptr<DBConnection> conn) {
    if (!conn) {
        return;
    }
    
    std::unique_lock<std::mutex> lock(m_mutex);
    
    if (!m_running) {
        return;
    }
    
    // 归还连接到队列
    m_connectionQueue.push(conn);
    m_activeConnCount--;
    
    // 通知等待的线程
    m_cv.notify_one();
}

std::shared_ptr<DBConnection> DBConnectionPool::createConnection() {
    auto conn = std::make_shared<DBConnection>(m_host, m_user, m_password, m_dbName, m_port);
    if (!conn->connect()) {
        return nullptr;
    }
    return conn;
}

void DBConnectionPool::close() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        
        if (!m_running) {
            return;
        }
        
        m_running = false;
        
        // 清空连接队列
        while (!m_connectionQueue.empty()) {
            m_connectionQueue.pop();
        }
        
        m_totalConnCount = 0;
        m_activeConnCount = 0;
    }
    
    // 通知所有等待的线程
    m_cv.notify_all();
    
    // 等待保活线程结束
    if (m_keepAliveThread.joinable()) {
        m_keepAliveThread.join();
    }
}

void DBConnectionPool::keepAliveThreadFunc() {
    while (m_running) {
        std::this_thread::sleep_for(std::chrono::seconds(60)); // 每60秒检查一次
        
        std::unique_lock<std::mutex> lock(m_mutex);
        
        if (!m_running) {
            break;
        }
        
        size_t queueSize = m_connectionQueue.size();
        size_t checkCount = std::min(queueSize, static_cast<size_t>(5)); // 每次最多检查5个连接
        
        for (size_t i = 0; i < checkCount; ++i) {
            auto conn = m_connectionQueue.front();
            m_connectionQueue.pop();
            
            lock.unlock();
            
            bool alive = conn->ping();
            
            lock.lock();
            
            if (alive) {
                m_connectionQueue.push(conn);
            } else {
                if (conn->reconnect()) {
                    m_connectionQueue.push(conn);
                } else {
                    // 连接无法恢复，减少计数
                    m_totalConnCount--;
                }
            }
        }
    }
}

size_t DBConnectionPool::getActiveConnectionCount() const {
    return m_activeConnCount.load();
}

size_t DBConnectionPool::getIdleConnectionCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_connectionQueue.size();
}

size_t DBConnectionPool::getTotalConnectionCount() const {
    return m_totalConnCount.load();
}

} // namespace db
} // namespace kama
