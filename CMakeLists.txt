# 设置最低版本和项目名称
cmake_minimum_required(VERSION 3.0)
project(kama-webserver)

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

# ==================== 构建 LLaMA Service 可执行文件 ====================
file(GLOB LlamaService_SRC src/llama_service.cc memory/*.cc log/*.cc src/Thread.cc)
list(FILTER LlamaService_SRC EXCLUDE REGEX ".*main\\.cc$")
add_executable(llama_service ${LlamaService_SRC})
target_link_libraries(llama_service ${LIBS} current_thread)
target_include_directories(llama_service PRIVATE 
    ${CMAKE_SOURCE_DIR}/include 
    ${CMAKE_SOURCE_DIR}/memory 
    ${CMAKE_SOURCE_DIR}/log
)
if (USE_CUDA)
    target_compile_definitions(llama_service PRIVATE USE_CUDA)
    target_link_libraries(llama_service ${CUDA_LIBRARIES})
    message(STATUS "✅ Compiling LLaMA Service with CUDA support.")
endif()

# ==================== 构建 LLaMA TCP 服务可执行文件 ====================
file(GLOB LlamaServiceTCP_SRC src/llama_service_tcp.cc memory/*.cc log/*.cc src/Thread.cc)
list(FILTER LlamaServiceTCP_SRC EXCLUDE REGEX ".*main\\.cc$")
add_executable(llama_service_tcp ${LlamaServiceTCP_SRC})
target_link_libraries(llama_service_tcp ${LIBS} current_thread)
target_include_directories(llama_service_tcp PRIVATE 
    ${CMAKE_SOURCE_DIR}/include 
    ${CMAKE_SOURCE_DIR}/memory 
    ${CMAKE_SOURCE_DIR}/log
)
if (USE_CUDA)
    target_compile_definitions(llama_service_tcp PRIVATE USE_CUDA)
    target_link_libraries(llama_service_tcp ${CUDA_LIBRARIES})
    message(STATUS "✅ Compiling LLaMA TCP Service with CUDA support.")
endif()

# ==================== 构建 KamaWebServer 可执行文件 ====================
# 为KamaWebServer指定源文件，排除所有包含main函数的文件和重复的CurrentThread实现
file(GLOB WebServer_SRC src/*.cc)
list(FILTER WebServer_SRC EXCLUDE REGEX ".*main_http\\.cc$")
list(FILTER WebServer_SRC EXCLUDE REGEX ".*llama_service\\.cc$") 
list(FILTER WebServer_SRC EXCLUDE REGEX ".*llama_service_tcp\\.cc$")
list(FILTER WebServer_SRC EXCLUDE REGEX ".*CurrentThread\\.cc$") # 排除src中的CurrentThread.cc

file(GLOB MEMORY_SRC memory/*.cc)
file(GLOB LOG_SRC log/*.cc)
list(FILTER LOG_SRC EXCLUDE REGEX ".*CurrentThread\\.cc$") # 排除log中的CurrentThread.cc

# 添加配置管理源文件
set(CONFIG_SRC ${CMAKE_SOURCE_DIR}/src/ConfigManager.cc)

# 构建KamaWebServer
add_executable(KamaWebServer ${WebServer_SRC} ${MEMORY_SRC} ${LOG_SRC} ${CONFIG_SRC})
target_link_libraries(KamaWebServer ${LIBS} current_thread) # 使用current_thread库
target_include_directories(KamaWebServer PRIVATE 
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/memory
    ${CMAKE_SOURCE_DIR}/log
)

if (USE_CUDA)
    target_compile_definitions(KamaWebServer PRIVATE USE_CUDA)
    target_link_libraries(KamaWebServer ${CUDA_LIBRARIES})
    message(STATUS "✅ Compiling KamaWebServer with CUDA support.")
endif()

# ==================== HTTP服务器构建 ====================
file(GLOB HTTP_SOURCES "src/http/*.cc")
file(GLOB HTTP_MEMORY_SRC "memory/*.cc")
file(GLOB HTTP_LOG_SRC "log/*.cc")

# 获取基础网络库源文件，但排除多个main函数和HTTP文件
file(GLOB NETWORK_SRC "src/*.cc")
list(FILTER NETWORK_SRC EXCLUDE REGEX ".*main\\.cc$")
list(FILTER NETWORK_SRC EXCLUDE REGEX ".*main_http\\.cc$")
list(FILTER NETWORK_SRC EXCLUDE REGEX ".*llama_service\\.cc$") 
list(FILTER NETWORK_SRC EXCLUDE REGEX ".*llama_service_tcp\\.cc$")
list(FILTER NETWORK_SRC EXCLUDE REGEX ".*CurrentThread\\.cc$")

# 排除可能导致重复定义的文件
list(FILTER HTTP_LOG_SRC EXCLUDE REGEX ".*CurrentThread\\.cc$")

# 构建HTTP服务器 - 包含基础网络库源文件
add_executable(kama_http_server 
    src/main_http.cc 
    src/db/DatabaseManager.cpp  # 确保这行存在
    src/ConfigManager.cc       # 添加配置管理器
    ${HTTP_SOURCES}
    ${HTTP_MEMORY_SRC}
    ${HTTP_LOG_SRC}
    ${NETWORK_SRC}  # 添加网络库源文件
)

target_link_libraries(kama_http_server 
    ${LIBS} 
    current_thread  # 直接链接已经构建的CurrentThread库
)

target_include_directories(kama_http_server PRIVATE 
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/include/http
    ${CMAKE_SOURCE_DIR}/memory
    ${CMAKE_SOURCE_DIR}/log
)

if (USE_CUDA)
    target_compile_definitions(kama_http_server PRIVATE USE_CUDA)
    target_link_libraries(kama_http_server ${CUDA_LIBRARIES})
endif()

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
target_link_libraries(kama_http_server
    ${LIBS} 
    current_thread
    ${MYSQL_LIBRARY}
    ssl
    crypto
)

message(STATUS "✅ Configuring HTTP Server build with MySQL support.")
