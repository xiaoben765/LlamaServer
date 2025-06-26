#pragma once

#include <memory>
#include <functional>

class Buffer;
class TcpConnection;
class Timestamp;

using TcpConnectionPtr = std::shared_ptr<TcpConnection>; // 智能指针，用于管理 TcpConnection 对象的生命周期
using ConnectionCallback = std::function<void(const TcpConnectionPtr &)>; // 回调函数类型，当有新连接建立或连接断开时调用
using CloseCallback = std::function<void(const TcpConnectionPtr &)>; // 回调函数类型，当连接关闭时调用
using WriteCompleteCallback = std::function<void(const TcpConnectionPtr &)>; // 回调函数类型，当数据写入完成时调用
using HighWaterMarkCallback = std::function<void(const TcpConnectionPtr &, size_t)>; // 回调函数类型，当发送缓冲区达到高水位时调用

using MessageCallback = std::function<void(const TcpConnectionPtr &,
                                           Buffer *,
                                           Timestamp)>; // 回调函数类型，当有新消息到达时调用，参数包括连接指针、缓冲区和时间戳