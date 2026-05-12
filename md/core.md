# Core 模块设计文档

## 概述

Core 模块是 RPC 框架的"组装层"，将网络层、协议层、服务管理层、线程池等模块组合为可直接使用的 RPC 客户端和服务端。服务端支持多线程并发处理，客户端支持多线程并发调用。

## 文件位置

| 文件 | 用途 |
|------|------|
| `src/core/rpc_service.h/cpp` | RPC 服务端：组合 MessageCycle + ServiceManager + ThreadPool |
| `src/core/rpc_client.h/cpp` | RPC 客户端：连接管理、请求发送、响应匹配、写队列 |
| `src/core/error_code.h` | 统一错误码枚举 |
| `src/server_main.cpp` | 服务端入口程序 |
| `src/client_main.cpp` | 客户端入口程序 |

---

## 架构总览

```
 ┌────────────────────────────────────────────────────────────────┐
 │                      server_main.cpp                           │
 │   PingService  ←─ 业务服务实现                                  │
 ├────────────────────────────────────────────────────────────────┤
 │                      RpcServer                                 │
 │   ┌─────────────┐  ┌──────────────┐  ┌──────────────────────┐  │
 │   │ MessageCycle │  │ServiceManager│  │     ThreadPool       │  │
 │   │ (accept+读)  │  │ (dispatch)   │  │ (并发处理)            │  │
 │   └──────┬───────┘  └──────┬───────┘  └──────────┬───────────┘  │
 │          │                 │                      │              │
 │   Connection           Service.handle()     worker threads      │
 └────────────────────────────────────────────────────────────────┘

 ┌────────────────────────────────────────────────────────────────┐
 │                      client_main.cpp                           │
 ├────────────────────────────────────────────────────────────────┤
 │                      RpcClient                                 │
 │   ┌──────────┐  ┌──────────────┐  ┌──────────────────────────┐ │
 │   │ connect  │  │  write queue │  │     read loop             │ │
 │   │ (握手)   │  │ (序列化写入) │  │ (持续读取响应+匹配)       │ │
 │   └──────────┘  └──────────────┘  └──────────────────────────┘ │
 │                                                                 │
 │   pending_ map: seq_id → std::promise<RpcResponse>              │
 └────────────────────────────────────────────────────────────────┘
```

---

## RpcServer

### 组件组合

```
RpcServer
  ├── MessageCycle (事件循环 + accept + 每连接读循环)
  ├── ServiceManager (单例，服务注册/查找/分发)
  └── ThreadPool* (可选，nullptr 时同步处理)
```

### 处理流程

```
1. Connection 读到完整帧 → on_message(header, body, conn)
2. 如果有 ThreadPool → enqueue 到线程池
     否则 → 直接在 io_context 线程处理
3. process_request():
   a. 验证 msg_type == REQUEST
   b. 反序列化 RpcRequest
   c. ServiceManager::dispatch(service, method, params)
   d. 返回 RpcResponse (error_code + result)
4. send_response(): 构造响应帧头 → conn->send()
```

### 线程安全

- `register_service()` → ServiceManager 内部使用 shared_mutex
- `on_message()` → 无共享状态，Connection::send() 自带写队列锁
- `send_response()` 可从任意线程调用（线程池 worker 或 io_context 线程）

### 使用示例

```cpp
rpc::ThreadPool pool(8);
rpc::RpcServer server;
server.set_thread_pool(&pool);
server.register_service(std::make_shared<MyService>());
server.start(8080);          // 阻塞当前线程
// ... 收到停止信号后 ...
server.stop();
```

---

## RpcClient

### 核心设计

客户端维护一个后台 io_context 线程，所有网络 IO 都在该线程执行：

```
io_thread:  ios_.run()
  ├── 持续读取循环 (start_read_loop → do_read_header → do_read_body → 循环)
  └── 写入队列 (enqueue_write → do_write chain)
```

### 读取循环

连接建立后立即启动，持续读取响应帧：

```
do_read_header()
  → async_read(20 bytes)
  → deserialize_header()
  → do_read_body()
    → async_read(body_size bytes)
    → deserialize response
    → match pending_[seq_id] → set_value(promise)
    → do_read_header()  // 循环
```

任何读取错误都会 reject_all_pending 并停止循环。

### 写入队列

多个线程可同时调用 `call()`，写入操作被序列化：

```
call() (thread A)          call() (thread B)
  │                          │
  ├─ push write_queue_       ├─ push write_queue_
  ├─ acquire write lock      └─ return (A is writing)
  └─ do_write()
      ├─ async_write item A
      └─ callback → do_write() (picks up item B)
```

### 请求/响应匹配

```
call("Foo", "Bar", params)
  → seq_id = 42 (原子递增)
  → pending_[42] = promise
  → 构造 Request 帧 → 写入队列
  → return future

... 服务器处理 ...

read loop 收到 Response (seq_id=42)
  → pending_.find(42) → 取出 promise
  → promise.set_value(response)
  → future.get() 返回
```

### 线程安全

| 操作 | 安全机制 |
|------|----------|
| `call()` | pending_mux_ 保护 pending_ map；write_mux_ 序列化写入 |
| `connect()` | 在 io_thread 完成；promise 跨线程安全 |
| `disconnect()` | 停止 io_context，join io_thread |
| `is_connected()` | atomic<bool> |

### 使用示例

```cpp
rpc::RpcClient client;
client.connect("127.0.0.1", 8080).get();

// 单线程调用
auto resp = client.call("PingService", "Ping", "hello").get();

// 多线程并发调用（安全）
std::vector<std::future<rpc::RpcResponse>> futs;
for (int i = 0; i < 100; i++) {
    futs.push_back(client.call("Svc", "Method", "data" + std::to_string(i)));
}
for (auto& f : futs) { f.get(); }

client.disconnect();
```

---

## 错误码

| 错误码 | 值 | 说明 |
|--------|-----|------|
| `OK` | 0 | 成功 |
| `SERVICE_NOT_FOUND` | -1 | 目标服务未注册 |
| `METHOD_NOT_FOUND` | -2 | 目标方法不存在 |
| `TIMEOUT` | -3 | 调用超时 |
| `SERIALIZE_ERROR` | -4 | 序列化失败 |
| `DESERIALIZE_ERROR` | -5 | 反序列化失败 |
| `CONNECTION_CLOSED` | -1000 | 连接已断开 |
| `INTERNAL_ERROR` | -9999 | 内部错误 |

---

## 线程模型

### 服务端

```
main thread
  ├── 信号等待 (sleep loop)
  └── server.stop() → 优雅关闭

server thread (MessageCycle::run)
  └── io_context.run()
      ├── acceptor.async_accept → do_accept 循环
      └── 每个连接: do_read_header → do_read_body → on_message → 循环

thread pool workers (N 个)
  └── 每个 worker:
      ├── 从队列取任务
      ├── process_request() → ServiceManager::dispatch()
      └── send_response() → conn->send()
```

### 客户端

```
main thread
  └── 发起 call() / 等待响应

client io thread
  └── io_context.run()
      ├── 持续读取循环 (read loop)
      └── 写入队列处理 (write chain)
```

---

## 测试覆盖

| 测试文件 | 用例数 | 覆盖内容 |
|----------|--------|----------|
| `network_test.cpp` | 14 | ConnectionManager、RpcServer 启停、客户端连接、基本调用、错误码、大负载、多调用 |
| `integration_test.cpp` | 10 | ThreadPool 集成、Ping/Stats 调用、错误分发、顺序调用、并发调用（4线程×25）、断连重连、大负载（100KB）、同步模式 |

运行测试：
```bash
cd build
./network_test.exe
./integration_test.exe
```
