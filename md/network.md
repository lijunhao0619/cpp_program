# 网络模块设计文档

## 概述

RPC 网络模块基于 Boost.Asio 实现跨平台异步网络 IO，提供 TCP 服务端（监听、接受连接、读写帧）和客户端（连接、收发请求）的完整能力。模块内部使用 Reactor 模式（Linux epoll / Windows IOCP），对外暴露异步回调接口。

## 文件位置

| 文件 | 用途 |
|------|------|
| `src/network/create_socket.h/cpp` | Socket 创建工具（服务端 acceptor、客户端连接） |
| `src/network/connection.h/cpp` | 单条 TCP 连接：异步读写 RPC 协议帧 |
| `src/network/connection_manager.h/cpp` | 连接池：线程安全地管理所有活跃连接 |
| `src/network/message_cycle.h/cpp` | 消息事件循环：io_context 驱动、接受连接、分发消息 |
| `src/core/rpc_service.h/cpp` | RPC 服务器：组合 MessageCycle + ServiceManager |
| `src/core/rpc_client.h/cpp` | RPC 客户端：连接、发送请求、匹配响应 |
| `src/core/error_code.h` | 统一错误码定义 |

---

## 架构设计

### 分层架构

```
 ┌──────────────────────────────────────────────┐
 │                  应用层                       │
 │   RpcServer / RpcClient                      │
 ├──────────────────────────────────────────────┤
 │                  事件循环层                   │
 │   MessageCycle (accept + dispatch)           │
 ├──────────────────────────────────────────────┤
 │                  连接层                       │
 │   Connection | ConnectionManager             │
 ├──────────────────────────────────────────────┤
 │                  传输层                       │
 │   Boost.Asio (TCP / epoll / IOCP)            │
 └──────────────────────────────────────────────┘
```

### 服务端架构

```
RpcServer::start(port)
    │
    ├── MessageCycle::start_listen(port, handler)
    │       ├── acceptor_.listen(256)
    │       └── do_accept() ◄──────────────────┐
    │              │                            │
    │              ▼                            │
    │       acceptor_.async_accept()           │
    │              │                            │
    │              ▼ (新连接到达)               │
    │       Connection(socket)                 │
    │              │                            │
    │       conn_mgr_.add(conn)                │
    │              │                            │
    │       conn->start_read(handler) ──────────┼──► do_read_header()
    │              │                            │         │
    │       do_accept() ────────────────────────┘         ▼
    │                                              do_read_body()
    │                                                    │
    │                                                    ▼
    │                                              handler(header, body, conn)
    │                                                    │
    │                                              RpcServer::on_message()
    │                                                    │
    │                                              ServiceManager::dispatch()
    │                                                    │
    │                                              conn->send(header, body)
    │
    └── cycle_.run() → ios_.run() (阻塞事件循环)
```

### 客户端架构

```
RpcClient::connect(host, port)
    │
    ├── resolver_.async_resolve()
    ├── async_connect(socket_)
    ├── io_thread_ = std::thread([ios_.run()])
    └── future.get() → 连接成功

RpcClient::call(service, method, params)
    │
    ├── 构造 RpcRequest + RpcHeader
    ├── 注册 pending_[seq_id]
    ├── async_write(frame)
    │       │
    │       ▼ (写完成)
    ├── do_read_response(seq_id)
    │       │
    │       ▼ (读帧头 20 字节)
    │       do_read_body()
    │       │
    │       ▼ (读 body)
    │       解析 RpcResponse → 匹配 pending_[seq_id]
    │       promise.set_value(resp)
    │
    └── future.get() → RpcResponse
```

---

## API 设计

### CreateSocket

```cpp
// 创建服务端监听 acceptor，绑定端口，设置 SO_REUSEADDR
boost::asio::ip::tcp::acceptor create_server_acceptor(
    boost::asio::io_context& ios, uint16_t port);

// 异步连接远程服务器，返回已连接的 socket（future）
std::future<boost::asio::ip::tcp::socket> create_client_socket(
    boost::asio::io_context& ios,
    const std::string& host, uint16_t port);
```

### Connection

```cpp
class Connection : public std::enable_shared_from_this<Connection> {
public:
    using MessageHandler = std::function<void(
        const RpcHeader&, std::string body,
        std::shared_ptr<Connection>)>;
    using CloseHandler = std::function<void(std::shared_ptr<Connection>)>;

    explicit Connection(boost::asio::ip::tcp::socket socket);

    // 开始读取消息循环
    void start_read(MessageHandler on_message, CloseHandler on_close = nullptr);

    // 发送响应（帧头 + body，自动序列化帧头）
    void send(const RpcHeader& header, const std::string& body);

    // 关闭连接（线程安全，幂等）
    void close();

    bool is_open() const;
    uint64_t conn_id() const;

private:
    // 读取循环：do_read_header() → do_read_body() → handler → do_read_header() ...
    // 写入队列：send() → do_write() （异步链式写入，防止并发写）
};
```

### ConnectionManager

```cpp
class ConnectionManager {
public:
    uint64_t add(std::shared_ptr<Connection> conn);      // 分配 ID 并加入池
    void remove(uint64_t conn_id);                        // 按 ID 移除
    std::shared_ptr<Connection> get(uint64_t conn_id);    // 按 ID 查找
    size_t count();                                        // 连接数
    std::vector<std::shared_ptr<Connection>> all_connections();
    void close_all();                                     // 关闭所有连接
};
```

### MessageCycle

```cpp
class MessageCycle {
public:
    // 开始监听端口，注册消息回调
    void start_listen(uint16_t port, MessageHandler handler);

    // 运行事件循环（阻塞当前线程）
    void run();

    // 后台线程中运行
    void run_async();

    // 停止事件循环（关闭 acceptor、断开所有连接、停止 io_context）
    void stop();

    boost::asio::io_context& io_context();
    ConnectionManager& connections();
    bool is_running() const;
};
```

### RpcServer

```cpp
class RpcServer {
public:
    // 注册业务服务
    void register_service(std::shared_ptr<Service> service);

    // 启动服务器（阻塞，调用 ios_.run()）
    void start(uint16_t port);

    // 停止服务器
    void stop();

    bool is_running() const;
};
```

### RpcClient

```cpp
class RpcClient {
public:
    // 连接远程服务器（返回 future，完成时表示连接已建立）
    std::future<void> connect(const std::string& host, uint16_t port);

    // 发起 RPC 调用（返回 future<RpcResponse>）
    std::future<RpcResponse> call(const std::string& service,
                                  const std::string& method,
                                  const std::string& params,
                                  uint32_t timeout_ms = 5000);

    // 断开连接
    void disconnect();

    bool is_connected() const;
};
```

---

## 使用示例

### 服务端

```cpp
#include "core/rpc_service.h"
#include "service/service_manager.h"

int main() {
    rpc::RpcServer server;

    // 注册服务
    auto user_svc = std::make_shared<UserService>();
    server.register_service(user_svc);

    // 启动（阻塞当前线程）
    server.start(8080);

    return 0;
}
```

### 客户端

```cpp
#include "core/rpc_client.h"

int main() {
    rpc::RpcClient client;

    // 连接服务端
    auto conn_fut = client.connect("127.0.0.1", 8080);
    conn_fut.get();
    std::cout << "Connected!" << std::endl;

    // 发起调用
    auto resp_fut = client.call("UserService", "Login",
                                serialized_login_params);
    auto resp = resp_fut.get();

    if (resp.error_code == 0) {
        std::cout << "Login success: " << resp.result << std::endl;
    } else {
        std::cout << "Error: " << resp.error_msg << std::endl;
    }

    client.disconnect();
    return 0;
}
```

---

## 线程模型

```
服务端:
  io_context 线程 ×1
      ├── 接受连接
      ├── 读取所有连接的帧
      ├── 调用 on_message 回调（同步，在 io_context 线程中）
      ├── 写入响应
      └── 连接断开清理

客户端:
  io_context 线程 ×1  ← (work_guard 保持存活)
      ├── 发起连接
      ├── 发送请求帧
      ├── 读取响应帧
      └── 匹配 pending 请求

  main 线程:
      ├── connect() → future.get() 等待连接
      └── call()   → future.get() 等待响应
```

**线程安全保证**：
- ConnectionManager: `std::shared_mutex`（读共享，写互斥）
- Connection: `close_` flag 用 `std::atomic` + CAS 保证幂等
- Connection: 写队列内部 `std::mutex` 保护
- RpcClient: pending map 用 `std::mutex` 保护

---

## 关键设计决策

### 1. 连接生命周期管理

- **引用计数**：`enable_shared_from_this<Connection>` 确保异步回调中连接对象有效
- **关闭幂等**：`closed_` 原子标志通过 CAS 确保 `close()` / `on_close_` 只执行一次
- **连接清理**：`ConnectionManager::close_all()` 先取出并清空 map，再释放锁后逐个关闭，避免死锁

### 2. 异步写入队列

`Connection::send()` 使用内部写队列保证：
- 多个 `send()` 调用不会并发写 socket
- 写入按调用顺序串行化
- 写入失败时统一通知关闭回调

### 3. IO Context 生命周期

- **服务端**：`async_accept` 始终有一个在 pending，保持 `io_context` 存活
- **客户端**：使用 `executor_work_guard` 保持 `io_context` 不提前退出
- **关闭**：先关闭 connections → reset work_guard → `ios_.stop()`

---

## 错误码

| 错误码 | 含义 |
|--------|------|
| 0 | 成功 |
| -1 | 服务不存在 |
| -2 | 方法不存在 |
| -3 | 调用超时 |
| -4 | 序列化失败 |
| -5 | 反序列化失败 |
| -10 | 连接已关闭 |
| -11 | 连接被拒绝 |
| -99 | 内部错误 |

---

## 测试结果

| 测试 | 内容 | 结果 |
|------|------|------|
| ConnectionManager add/count | 添加连接并计数 | OK |
| ConnectionManager get | 按 ID 获取连接 | OK |
| ConnectionManager remove | 移除连接后不可访问 | OK |
| ConnectionManager close_all | 关闭所有连接并清空 | OK |
| error_code_str | 错误码转字符串 | OK |
| RPC server start/stop | 服务端启动和停止 | OK |
| client connect | 客户端连接服务端 | OK |
| RPC call success | Echo 调用往返 | OK |
| RPC call not found | 不存在的服务返回错误 | OK |
| RPC call bad method | 不存在的方法返回错误 | OK |
| RPC call large payload | 100KB 大负载往返 | OK |
| multiple sequential | 10 次连续调用 | OK |
| empty params | 空参数调用 | OK |

---

## 编译依赖

- **Boost.Asio**（header-only，MSYS2 安装）
- **Windows**: 链接 `ws2_32 wsock32`
- **Linux**: 链接 `pthread`
- 无其他外部依赖
