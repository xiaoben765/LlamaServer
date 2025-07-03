# 高并发处理模块

本项目基于 [youngyangyang04/kama-webserver](https://github.com/youngyangyang04/kama-webserver) 进行了大量扩展，从简单的回声服务器升级为**支持 AI 模型推理的高并发服务平台**。实现了一系列企业级高并发处理功能，用于支持高性能的Web服务器和LLaMA模型推理服务。

## 🚀 相比原项目的主要改进

- **功能升级**: 从回声服务器 → AI 推理服务平台
- **架构革新**: 基础网络模型 → 异步任务队列 + 实例池 + 负载均衡
- **可靠性**: 基础容错 → 故障转移 + 自动恢复 + 健康检查
- **扩展性**: 单体架构 → 模块化设计 + 接口抽象
- **性能**: 同步处理 → 异步并发处理，支持数千并发请求

## 主要组件

### 1. 异步任务队列 (AsyncTaskQueue)

提供基于线程池的异步任务执行机制，支持任务提交和结果获取。

- 文件: `include/AsyncTaskQueue.h` 和 `src/AsyncTaskQueue.cc`
- 特性:
  - 线程池管理
  - 任务异步提交
  - Future/Promise 结果返回
  - 优雅关闭

### 2. 模型实例池 (ModelInstancePool)

管理多个LLaMA服务实例，支持负载均衡和故障转移。

- 文件: `include/services/ModelInstancePool.h` 和 `src/services/ModelInstancePool.cc`
- 特性:
  - 多实例管理
  - 负载均衡 (Round-Robin)
  - 实例健康检查
  - 故障检测和恢复
  - 自动重连和重建

### 3. 异步LLaMA服务 (AsyncLlamaService)

提供异步的LLaMA模型推理接口，支持非阻塞调用和回调。

- 文件: `include/services/AsyncLlamaService.h` 和 `src/services/AsyncLlamaService.cc`
- 特性:
  - 异步查询接口
  - 超时控制
  - 完成回调
  - 自动实例获取和释放

### 4. 高并发HTTP服务器 (HighConcurrentHttpServer)

基于事件驱动的高并发HTTP服务器，支持同步和异步请求处理。

- 文件: `include/http/HighConcurrentHttpServer.h` 和 `src/http/HighConcurrentHttpServer.cc`
- 特性:
  - 多线程处理
  - 同步处理器注册
  - 异步处理器注册
  - 基于回调的异步响应

## 模拟服务

为了便于测试和开发，项目包含了一个模拟LLaMA服务:

### LlamaMockService

模拟LLaMA服务，用于在没有实际LLaMA服务的情况下测试高并发功能。

- 文件: `include/services/LlamaMockService.h`
- 特性:
  - 可配置的响应延迟
  - 可配置的故障率
  - 兼容ILlamaService接口
  - 支持模拟连接重置和可用性检查

使用模拟服务可以在不依赖实际LLaMA服务的情况下测试以下功能:
- 模型实例池的负载均衡
- 模型实例池的故障转移和恢复
- 异步LLaMA服务的并发处理
- 高并发场景下的系统稳定性

### 模拟服务初始化

模拟服务可以通过两种方式初始化:

```cpp
// 在ModelInstancePool中:

// 直接使用模拟服务初始化模型实例池 (推荐)
bool success = pool.initWithMockServices(instanceCount, delayMs, failRate);

// 或手动创建模拟服务实例
auto mockService = std::make_shared<LlamaMockService>(delayMs, failRate);
```

参数说明:
- `instanceCount`: 模拟实例数量
- `delayMs`: 模拟处理延迟(毫秒)
- `failRate`: 模拟失败率(0-100的整数)

## 测试程序

`test_concurrency.cc` 包含了对以上组件的全面测试。

- 异步任务队列测试: 验证多任务并发执行和结果返回
- 模型实例池测试: 测试实例获取、释放和故障报告
- 异步LLaMA服务测试: 测试异步查询和回调
- 高并发HTTP服务器测试: 测试HTTP请求处理和响应

## 构建和运行

使用以下命令构建和运行测试:

```bash
# 构建测试程序
make test_concurrency

# 运行所有测试
./bin/test_concurrency

# 或者运行特定测试
./bin/test_concurrency --async  # 测试异步任务队列
./bin/test_concurrency --pool   # 测试模型实例池
./bin/test_concurrency --service # 测试异步LLaMA服务
./bin/test_concurrency --http   # 测试高并发HTTP服务器
```

## 自动化测试

项目提供了两个测试脚本:

### 全面测试脚本

```bash
./test_concurrency.sh [--http]
```

- 按顺序测试异步任务队列、模型实例池和异步LLaMA服务
- 添加 `--http` 参数可选择性测试HTTP服务器

### 专用异步LLaMA服务测试脚本

```bash
./test_async_llama.sh
```

- 专门用于测试异步LLaMA服务和模拟服务集成
- 包含详细的日志和测试结果报告

## 注意事项

- 实际模型实例池测试需要有可用的LLaMA服务实例
- 如果实际LLaMA服务不可用，测试会自动使用模拟服务
- 模拟服务可以通过调整延迟和故障率参数来模拟不同的负载场景
- 测试过程中如遇到"服务不可用"问题，可尝试以下方法:
  - 确保异步任务队列已经初始化
  - 检查模型实例池初始化是否成功
  - 验证模拟服务可用性设置
  - 尝试先 `shutdown()` 后再重新初始化模型实例池
- HTTP服务器测试会启动一个监听8080端口的服务器，按Ctrl+C退出
