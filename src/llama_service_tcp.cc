#include "services/LlamaTcpServer.h"
#include <iostream>

int main(int argc, char *argv[]) {
    try {
        llama::LlamaTcpServer server;
        
        if (!server.initialize(argc, argv)) {
            std::cerr << "❌ 服务器初始化失败" << std::endl;
            return -1;
        }
        
        return server.run();
    } catch (const std::exception& e) {
        std::cerr << "❌ 程序异常: " << e.what() << std::endl;
        return -1;
    } catch (...) {
        std::cerr << "❌ 未知异常" << std::endl;
        return -1;
    }
}
