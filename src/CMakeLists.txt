#获取当前目录下的所有源文件项目
file(GLOB SRC_FILE ${CMAKE_CURRENT_SOURCE_DIR}/*cc)
list(REMOVE_ITEM SRC_FILE ${CMAKE_CURRENT_SOURCE_DIR}/main.cc)
list(REMOVE_ITEM SRC_FILE ${CMAKE_CURRENT_SOURCE_DIR}/main_http.cc)
list(REMOVE_ITEM SRC_FILE ${CMAKE_CURRENT_SOURCE_DIR}/llama_service.cc)
list(REMOVE_ITEM SRC_FILE ${CMAKE_CURRENT_SOURCE_DIR}/llama_service_tcp.cc)
list(REMOVE_ITEM SRC_FILE ${CMAKE_CURRENT_SOURCE_DIR}/main_http_modular_new.cc)
list(REMOVE_ITEM SRC_FILE ${CMAKE_CURRENT_SOURCE_DIR}/llama_service_tcp_new.cc)

# 获取HTTP相关源文件
file(GLOB HTTP_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/http/*.cc)

# 获取数据库相关源文件
file(GLOB DB_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/db/*.cc)

# 获取服务相关源文件
file(GLOB SERVICE_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/services/*.cc)

# 合并所有源文件
set(ALL_SRCS ${SRC_FILE} ${HTTP_SRCS} ${DB_SRCS} ${SERVICE_SRCS})

# 创建共享库
add_library(src_lib SHARED ${ALL_SRCS})

# 这里删除了可执行文件的定义，因为它们已经在主CMakeLists.txt中定义
# 只保留src_lib的创建
