#!/bin/bash

# 默认参数
LIVE_DEBUG=0
TIMEOUT=30
STRACE_MODE=0
if [ $STRACE_MODE -eq 1 ]; then
  # 使用strace跟踪系统调用
  echo "使用strace跟踪系统调用，超时限制为${TIMEOUT}秒..."
  timeout ${TIMEOUT}s strace -f -o strace_output.log ./bin/kama_http_server_modular
  EXIT_CODE=$?
  echo "strace跟踪完成，输出保存在strace_output.log"
  echo "最后的系统调用:"
  tail -n 20 strace_output.log
  
elif [ $VALGRIND_MODE -eq 1 ]; then
  # 使用valgrind检测内存问题
  echo "使用valgrind检测内存问题，超时限制为${TIMEOUT}秒..."
  timeout ${TIMEOUT}s valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --verbose --log-file=valgrind_output.log ./bin/kama_http_server_modular
  EXIT_CODE=$?
  echo "valgrind检测完成，输出保存在valgrind_output.log"
  
elif [ $LIVE_DEBUG -eq 1 ]; then
  # 实时调试模式
  echo "进入交互式调试，您可以在gdb提示符下输入命令..."
  gdb -ex "set confirm off" -ex "break main" -ex "run" -ex "continue" --args ./bin/kama_http_server_modular
  EXIT_CODE=$?
  
else
  # 检查服务器二进制文件是否存在
  if [ ! -f "./bin/kama_http_server_modular" ]; then
    echo "错误: 服务器二进制文件不存在，编译可能不完整"
    exit 1
  fi

  # 普通模式，限制30秒并进行更多调试
  echo "以非交互模式运行服务器，超时限制为${TIMEOUT}秒..."=0

# 解析命令行参数
while [[ $# -gt 0 ]]; do
  case $1 in
    --live)
      LIVE_DEBUG=1
      shift
      ;;
    --timeout)
      TIMEOUT=$2
      shift 2
      ;;
    --strace)
      STRACE_MODE=1
      shift
      ;;
    --valgrind)
      VALGRIND_MODE=1
      shift
      ;;
    *)
      shift
      ;;
  esac
done

echo "调试配置: 超时=${TIMEOUT}秒, 实时模式=$([[ $LIVE_DEBUG -eq 1 ]] && echo "是" || echo "否")"

# 清除旧的日志文件
rm -f debug_output.log

# 重新编译项目
echo "重新编译项目..."
mkdir -p build
cd build
cmake ..
make

# 检查编译状态
if [ $? -ne 0 ]; then
  echo "编译失败，请检查错误"
  exit 1
fi

echo "编译成功，准备运行服务器..."

# 尝试在调试模式下运行服务器，设置超时并保存输出
echo "启动模块化HTTP服务器..."
cd ..

# 使用timeout命令限制运行时间，避免无限挂起
if [ $LIVE_DEBUG -eq 1 ]; then
  # 实时调试模式
  echo "进入交互式调试，您可以在gdb提示符下输入命令..."
  gdb -ex "set confirm off" -ex "break main" -ex "run" -ex "continue" --args ./bin/kama_http_server_modular
else
  # 检查服务器二进制文件是否存在
  if [ ! -f "./bin/kama_http_server_modular" ]; then
    echo "错误: 服务器二进制文件不存在，编译可能不完整"
    exit 1
  fi

  # 普通模式，限制30秒并进行更多调试
  echo "以非交互模式运行服务器，超时限制为30秒..."
  
  # 使用预先定义的GDB命令文件 
  # (如果存在的话，否则创建一个基本的版本)
  if [ ! -f "gdb_commands.txt" ]; then
    cat > gdb_commands.txt <<EOF
set confirm off
set pagination off
set logging on
set logging file gdb_debug.log
set logging overwrite on

# 在主函数设置断点
break main
commands
  echo \n[*] 进入main函数\n\n
  continue
end

# 在常见卡住点设置断点
break accept
break poll
break epoll_wait

# 启动程序
run

# 等待5秒
shell sleep 5

# 收集调试信息
thread apply all bt full
info threads
info proc
EOF
  fi

  # 使用调试脚本运行gdb
  timeout ${TIMEOUT}s gdb -x gdb_commands.txt --args ./bin/kama_http_server_modular > debug_output.log 2>&1
  EXIT_CODE=$?

  # 检查退出状态
  if [ $EXIT_CODE -eq 124 ]; then
    echo "服务器运行超过${TIMEOUT}秒被强制终止，可能存在阻塞或无限循环"
    # 尝试捕获正在运行的进程信息
    PID=$(pgrep -f kama_http_server_modular)
    if [ ! -z "$PID" ]; then
      echo "发现运行中的服务器进程(PID: $PID)，尝试获取堆栈信息..."
      gdb -p $PID -ex "thread apply all bt" -ex "info threads" -ex "quit" >> debug_output.log 2>&1
      echo "已将堆栈信息添加到日志"
      echo "终止进程..."
      kill -9 $PID
    fi
  else
    echo "服务器已退出，退出码: $EXIT_CODE"
  fi

  # 输出最后30行日志以查看崩溃原因
  echo "===== 日志末尾内容 ====="
  tail -n 30 debug_output.log
fi

# 检查是否有core文件生成
echo "===== 检查core文件 ====="
find . -name "core*" -type f | xargs ls -l
