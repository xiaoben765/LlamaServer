# 设置最低版本和项目名称
cmake_minimum_required(VERSION 3.0)
project(llama-webserver)

# 设置全局的 C++ 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# 显式指定 CUDA 版本和路径（仅使用 12.8）
set(CUDA_TOOLKIT_ROOT_DIR /usr/local/cuda-12.8)
set(CMAKE_CUDA_COMPILER /usr/local/cuda-12.8/bin/nvcc)
find_package(CUDA 12.8 REQUIRED)

if (CUDA_FOUND)
    message(STATUS "✅ CUDA detected: ${CUDA_VERSION}")
    set(USE_CUDA TRUE)
else()
    message(FATAL_ERROR "❌ CUDA not detected. Please ensure CUDA is installed.")
endif()

# 设置头文件目录，供所有子项目使用
include_directories(
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/memory
    ${CMAKE_SOURCE_DIR}/log
    ${CUDA_INCLUDE_DIRS}
)

# 设置输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/lib)

# 创建配置目录
file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/config)

# 设置全局链接库
set(LIBS pthread ${CUDA_LIBRARIES})

# 添加 LLaMA.cpp 可执行文件路径（绝对路径）
set(LLAMA_CPP_PATH "/home/shl203/llama.cpp/build/bin/main")
set(LLAMA_MODEL_PATH "/home/shl203/llama.cpp/models/qwen/Qwen-7B-Chat.Q4_K_M.gguf")

# 确保 LLaMA.cpp 可执行文件和模型文件存在
if (EXISTS ${LLAMA_CPP_PATH} AND EXISTS ${LLAMA_MODEL_PATH})
    message(STATUS "✅ LLaMA.cpp and model found.")
else()
    message(FATAL_ERROR "❌ LLaMA.cpp or model file not found. Please ensure the paths are correct.")
endif()

# ==================== 构建 CurrentThread 静态库 ====================
add_library(current_thread STATIC src/CurrentThread.cc)
target_include_directories(current_thread PRIVATE ${CMAKE_SOURCE_DIR}/include)
message(STATUS "✅ Compiling CurrentThread as a static library.")

# ==================== 构建 LLaMA TCP 服务可执行文件 (已被模块化版本替代) ====================
# 原始版本已移动到 backup_original_files/ 目录
# file(GLOB LlamaServiceTCP_SRC src/llama_service_tcp.cc memory/*.cc log/*.cc src/Thread.cc)
# list(FILTER LlamaServiceTCP_SRC EXCLUDE REGEX ".*main\\.cc$")
# add_executable(llama_service_tcp ${LlamaServiceTCP_SRC})
# target_link_libraries(llama_service_tcp ${LIBS} current_thread)
# target_include_directories(llama_service_tcp PRIVATE 
#     ${CMAKE_SOURCE_DIR}/include 
#     ${CMAKE_SOURCE_DIR}/memory 
#     ${CMAKE_SOURCE_DIR}/log
# )
# if (USE_CUDA)
#     target_compile_definitions(llama_service_tcp PRIVATE USE_CUDA)
#     target_link_libraries(llama_service_tcp ${CUDA_LIBRARIES})
#     message(STATUS "✅ Compiling LLaMA TCP Service with CUDA support.")
# endif()

# ==================== 共享库文件和设置 ====================
# 准备共享文件和设置
file(GLOB MEMORY_SRC memory/*.cc)
file(GLOB LOG_SRC log/*.cc)
list(FILTER LOG_SRC EXCLUDE REGEX ".*CurrentThread\\.cc$") # 排除log中的CurrentThread.cc

# 添加配置管理源文件
set(CONFIG_SRC ${CMAKE_SOURCE_DIR}/src/ConfigManager.cc)

# ==================== 数据库库构建 ====================
set(DB_SOURCES 
    ${CMAKE_SOURCE_DIR}/src/db/DBConnectionPool.cc
    ${CMAKE_SOURCE_DIR}/src/db/DBQueryHelper.cc
    ${CMAKE_SOURCE_DIR}/src/db/DBIndexOptimizer.cc
    ${CMAKE_SOURCE_DIR}/src/db/DatabaseManager.cpp
)

# ==================== 高并发处理模块构建 ====================
set(CONCURRENCY_SOURCES
    ${CMAKE_SOURCE_DIR}/src/AsyncTaskQueue.cc
    ${CMAKE_SOURCE_DIR}/src/services/ModelInstancePool.cc
    ${CMAKE_SOURCE_DIR}/src/services/AsyncLlamaService.cc
    ${CMAKE_SOURCE_DIR}/src/services/LlamaService.cc
    ${CMAKE_SOURCE_DIR}/src/http/HighConcurrentHttpServer.cc
    ${CMAKE_SOURCE_DIR}/src/http/HttpContext.cc
    ${CMAKE_SOURCE_DIR}/src/http/HttpParser.cc
    ${CMAKE_SOURCE_DIR}/src/http/HttpRequest.cc
    ${CMAKE_SOURCE_DIR}/src/http/HttpResponse.cc
    ${CMAKE_SOURCE_DIR}/src/http/Middleware.cc
    ${CMAKE_SOURCE_DIR}/src/EventLoop.cc
    ${CMAKE_SOURCE_DIR}/src/InetAddress.cc
    ${CMAKE_SOURCE_DIR}/src/TcpServer.cc
    ${CMAKE_SOURCE_DIR}/src/TcpConnection.cc
    ${CMAKE_SOURCE_DIR}/src/Buffer.cc
    ${CMAKE_SOURCE_DIR}/src/Socket.cc
    ${CMAKE_SOURCE_DIR}/src/Channel.cc
    ${CMAKE_SOURCE_DIR}/src/Poller.cc
    ${CMAKE_SOURCE_DIR}/src/EPollPoller.cc
    ${CMAKE_SOURCE_DIR}/src/DefaultPoller.cc
    ${CMAKE_SOURCE_DIR}/src/Acceptor.cc
    ${CMAKE_SOURCE_DIR}/src/EventLoopThread.cc
    ${CMAKE_SOURCE_DIR}/src/EventLoopThreadPool.cc
    ${CMAKE_SOURCE_DIR}/src/Timer.cc
    ${CMAKE_SOURCE_DIR}/src/TimerQueue.cc
    ${CMAKE_SOURCE_DIR}/src/Timestamp.cc
    ${CMAKE_SOURCE_DIR}/src/Thread.cc
    ${CMAKE_SOURCE_DIR}/src/CurrentThread.cc
    ${CMAKE_SOURCE_DIR}/src/Logger.cc
    ${CMAKE_SOURCE_DIR}/log/AsyncLogging.cc
    ${CMAKE_SOURCE_DIR}/log/FileUtil.cc
    ${CMAKE_SOURCE_DIR}/log/LogFile.cc
    ${CMAKE_SOURCE_DIR}/log/LogStream.cc
)

# 检查文件是否存在
foreach(SRC_FILE ${DB_SOURCES})
    if(NOT EXISTS ${SRC_FILE})
        message(FATAL_ERROR "❌ 数据库源文件不存在: ${SRC_FILE}")
    endif()
endforeach()

add_library(db_lib STATIC ${DB_SOURCES})
target_include_directories(db_lib PRIVATE 
    ${CMAKE_SOURCE_DIR}/include
    ${MYSQL_INCLUDE_DIR}
)
target_link_libraries(db_lib ${MYSQL_LIBRARY})
message(STATUS "✅ 构建数据库静态库，包含优化的连接池和查询助手")

# 构建高并发处理库
add_library(concurrency_lib STATIC ${CONCURRENCY_SOURCES})
target_include_directories(concurrency_lib PRIVATE 
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/services
    ${CMAKE_SOURCE_DIR}/include/http
    ${CMAKE_SOURCE_DIR}/include/db
)
target_link_libraries(concurrency_lib pthread)
message(STATUS "✅ 构建高并发处理库，包含异步任务队列、模型实例池和异步服务")

# ==================== HTTP服务器构建 ====================
file(GLOB HTTP_MEMORY_SRC "memory/*.cc")
file(GLOB HTTP_LOG_SRC "log/*.cc")

# 获取基础网络库源文件，但排除多个main函数和HTTP文件
file(GLOB NETWORK_SRC "src/*.cc")
list(FILTER NETWORK_SRC EXCLUDE REGEX ".*main\\.cc$")
list(FILTER NETWORK_SRC EXCLUDE REGEX ".*main_http\\.cc$")
list(FILTER NETWORK_SRC EXCLUDE REGEX ".*main_http_modular\\.cc$") # 排除模块化main
list(FILTER NETWORK_SRC EXCLUDE REGEX ".*main_http_modular_new\\.cc$") # 排除新的模块化main
list(FILTER NETWORK_SRC EXCLUDE REGEX ".*llama_service\\.cc$") 
list(FILTER NETWORK_SRC EXCLUDE REGEX ".*llama_service_tcp\\.cc$")
list(FILTER NETWORK_SRC EXCLUDE REGEX ".*llama_service_tcp_new\\.cc$") # 排除新的TCP服务
list(FILTER NETWORK_SRC EXCLUDE REGEX ".*test_concurrency\\.cc$") # 排除测试文件
list(FILTER NETWORK_SRC EXCLUDE REGEX ".*CurrentThread\\.cc$")

# 排除可能导致重复定义的文件
list(FILTER HTTP_LOG_SRC EXCLUDE REGEX ".*CurrentThread\\.cc$")

# 显式列出HTTP相关源文件，确保包含所有必要文件
set(HTTP_SOURCES
    src/http/HttpServer.cc
    src/http/HttpContext.cc
    src/http/HttpParser.cc
    src/http/HttpRequest.cc
    src/http/HttpResponse.cc  # 确保包含 HttpResponse.cc
    src/http/Middleware.cc
)

message(STATUS "HTTP源文件 (显式列出): ${HTTP_SOURCES}")

# 构建HTTP服务器 (原始版本已被模块化版本替代)
# 原始版本已移动到 backup_original_files/ 目录
# add_executable(llama_http_server 
#     src/main_http_modular.cc 
#     src/db/DatabaseManager.cpp  # 确保这行存在
#     src/ConfigManager.cc       # 添加配置管理器
#     src/http/HttpServer.cc
#     src/http/HttpContext.cc
#     src/http/HttpParser.cc
#     src/http/HttpRequest.cc
#     src/http/HttpResponse.cc    # 显式包含，修复链接错误
#     src/http/Middleware.cc      # 显式包含，修复链接错误
#     src/services/DatabaseService.cc
#     src/services/LlamaService.cc
#     ${HTTP_MEMORY_SRC}
#     ${HTTP_LOG_SRC}
#     ${NETWORK_SRC}            # 网络库源文件
# )

# 构建HTTP服务器
add_executable(llama_http_server
    src/main_http_modular.cc
    src/http/LlamaHttpApplication.cc
    src/db/DatabaseManager.cpp
    src/ConfigManager.cc
    src/http/HttpServer.cc
    src/http/HttpContext.cc
    src/http/HttpParser.cc
    src/http/HttpRequest.cc
    src/http/HttpResponse.cc
    src/http/Middleware.cc
    src/services/DatabaseService.cc
    src/services/LlamaService.cc
    ${HTTP_MEMORY_SRC}
    ${HTTP_LOG_SRC}
    ${NETWORK_SRC}
)

# 构建TCP服务器
add_executable(llama_service_tcp
    src/llama_service_tcp.cc
    src/services/LlamaTcpServerConfig.cc
    src/services/LlamaTcpServerNetwork.cc
    src/services/LlamaTcpServerProcessor.cc
    src/services/LlamaTcpServerUtils.cc
)

# target_link_libraries(llama_http_server 
#     ${LIBS} 
#     current_thread  # 直接链接已经构建的CurrentThread库
#     db_lib         # 数据库库
#     concurrency_lib # 高并发处理库
#     ${MYSQL_LIBRARY}
#     ssl
#     crypto
#     z              # 添加zlib库用于压缩中间件
# )

target_link_libraries(llama_http_server
    ${LIBS} 
    current_thread
    db_lib
    concurrency_lib
    mysqlclient
    ssl
    crypto
    z
)

target_link_libraries(llama_service_tcp
    ${LIBS}
)

# target_include_directories(llama_http_server PRIVATE 
#     ${CMAKE_SOURCE_DIR}/include
#     ${CMAKE_SOURCE_DIR}/include/http
#     ${CMAKE_SOURCE_DIR}/memory
#     ${CMAKE_SOURCE_DIR}/log
# )

target_include_directories(llama_http_server PRIVATE 
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/http
    ${CMAKE_SOURCE_DIR}/include/services
    ${CMAKE_SOURCE_DIR}/include/db
    ${CMAKE_SOURCE_DIR}/memory
    ${CMAKE_SOURCE_DIR}/log
    ${MYSQL_INCLUDE_DIR}
)

target_include_directories(llama_service_tcp PRIVATE 
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/services
    ${CMAKE_SOURCE_DIR}/memory
    ${CMAKE_SOURCE_DIR}/log
)

# if (USE_CUDA)
#     target_compile_definitions(llama_http_server PRIVATE USE_CUDA)
#     target_link_libraries(llama_http_server ${CUDA_LIBRARIES})
# endif()

# 修改 MySQL 查找部分

# 直接使用 find_library 和 find_path 查找 MySQL
find_library(MYSQL_LIBRARY NAMES mysqlclient)
find_path(MYSQL_INCLUDE_DIR NAMES mysql.h PATH_SUFFIXES mysql)

if(NOT MYSQL_LIBRARY)
    message(FATAL_ERROR "❌ MySQL client library not found. Please install it with: sudo apt-get install libmysqlclient-dev")
endif()

if(NOT MYSQL_INCLUDE_DIR)
    message(FATAL_ERROR "❌ MySQL header files not found. Please install them with: sudo apt-get install libmysqlclient-dev")
endif()

message(STATUS "✅ MySQL library found at: ${MYSQL_LIBRARY}")
message(STATUS "✅ MySQL headers found at: ${MYSQL_INCLUDE_DIR}")

# 添加include目录
include_directories(
    ${MYSQL_INCLUDE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)

# 链接MySQL库以及其他需要的库
# target_link_libraries(llama_http_server
#     ${LIBS} 
#     current_thread
#     ${MYSQL_LIBRARY}
#     ssl
#     crypto
# )

# ==================== 构建模块化HTTP服务器 (已合并到 llama_http_server) ====================
# 已合并到 llama_http_server，不再单独构建

message(STATUS "✅ Configuring HTTP Server build with MySQL support.")

