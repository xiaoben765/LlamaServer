#include <mysql/mysql.h>
#include <iostream>
#include <string>

int main() {
    std::cout << "=== MySQL 连接测试 ===" << std::endl;
    
    // 初始化MySQL库
    if (mysql_library_init(0, nullptr, nullptr)) {
        std::cerr << "❌ MySQL库初始化失败" << std::endl;
        return 1;
    }
    
    // 创建MySQL连接对象
    MYSQL* connection = mysql_init(nullptr);
    if (!connection) {
        std::cerr << "❌ MySQL init失败" << std::endl;
        mysql_library_end();
        return 1;
    }
    
    // 设置连接参数
    std::string host = "localhost";
    std::string user = "root";
    std::string password = "password";
    std::string database = "kama_llm";
    int port = 3306;
    
    std::cout << "尝试连接到:" << std::endl;
    std::cout << "  主机: " << host << std::endl;
    std::cout << "  端口: " << port << std::endl;
    std::cout << "  用户: " << user << std::endl;
    std::cout << "  数据库: " << database << std::endl;
    
    // 尝试连接
    if (!mysql_real_connect(connection, 
                           host.c_str(),
                           user.c_str(),
                           password.c_str(),
                           database.c_str(),
                           port,
                           nullptr, 
                           0)) {
        
        unsigned int error_code = mysql_errno(connection);
        const char* error_msg = mysql_error(connection);
        
        std::cerr << "❌ 连接失败 [错误码: " << error_code << "]: " << error_msg << std::endl;
        
        // 提供解决建议
        switch (error_code) {
            case 1045:
                std::cerr << "💡 建议: 检查用户名和密码是否正确" << std::endl;
                break;
            case 1049:
                std::cerr << "💡 建议: 数据库不存在，请创建数据库: CREATE DATABASE kama_llm;" << std::endl;
                break;
            case 2003:
                std::cerr << "💡 建议: MySQL服务未运行，请启动MySQL服务" << std::endl;
                break;
            case 2005:
                std::cerr << "💡 建议: 检查主机名是否正确" << std::endl;
                break;
            default:
                std::cerr << "💡 建议: 检查MySQL配置和权限" << std::endl;
                break;
        }
        
        mysql_close(connection);
        mysql_library_end();
        return 1;
    }
    
    std::cout << "✅ MySQL连接成功!" << std::endl;
    std::cout << "服务器版本: " << mysql_get_server_info(connection) << std::endl;
    
    // 测试查询
    if (mysql_query(connection, "SELECT VERSION()") == 0) {
        MYSQL_RES* result = mysql_store_result(connection);
        if (result) {
            MYSQL_ROW row = mysql_fetch_row(result);
            if (row) {
                std::cout << "MySQL版本: " << row[0] << std::endl;
            }
            mysql_free_result(result);
        }
    }
    
    // 清理
    mysql_close(connection);
    mysql_library_end();
    
    std::cout << "=== 测试完成 ===" << std::endl;
    return 0;
}
