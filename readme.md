# RPC — C++ 高性能 RPC 框架

## 项目简介

RPC 是一个从零构建的 C++ RPC 通信框架，采用分层架构设计，将网络 IO、协议编解码、服务管理、线程调度等关注点彻底分离。框架基于 Boost.Asio 实现跨平台异步网络通信，支持 Protobuf/JSON 双序列化、zstd 压缩、AES-256-GCM 加密，并预留了 ZooKeeper 服务注册与发现的扩展能力。

### 核心特性

| 特性 | 实现 |
| ---- | ---- |
| 异步网络 IO | Boost.Asio（Windows IOCP / Linux epoll） |
| 自定义 RPC 协议 | 20 字节定长帧头 + 变长 Body，大端编码，CRC32 校验 |
| 双序列化器 | Protobuf 二进制 / JSON 文本，工厂模式切换 |
| 数据压缩 | zstd，可配置压缩阈值和级别 |
| 数据加密 | AES-256-GCM 认证加密（OpenSSL） |
| 并发处理 | 线程池，服务端可配置 worker 数量 |
| 服务管理 | 单例 ServiceManager，shared_mutex 线程安全 |
| 连接管理 | 连接池 + shared_ptr 生命周期 + 原子 close |
| 负载均衡 | 可插拔策略（Round Robin 等） |
| 日志系统 | spdlog，控制台彩色 + 文件滚动 |

---

## 架构设计

### 分层架构图

```text
 ┌─────────────────────────────────────────────────────────────────┐
 │                        应用入口层                                │
 │   server_main.cpp              client_main.cpp                   │
 │   PingService (业务实现)       调用示例                           │
 ├─────────────────────────────────────────────────────────────────┤
 │                        核心组装层 (core/)                        │
 │   ┌──────────────────┐       ┌──────────────────┐               │
 │   │    RpcServer      │       │    RpcClient      │               │
 │   │  ┌──────────────┐ │       │  ┌──────────────┐ │               │
 │   │  │ MessageCycle │ │       │  │  Read Loop   │ │               │
 │   │  │ ServiceMgr   │ │       │  │  Write Queue │ │               │
 │   │  │ ThreadPool   │ │       │  │  Pending Map │ │               │
 │   │  └──────────────┘ │       │  └──────────────┘ │               │
 │   └──────────────────┘       └──────────────────┘               │
 ├─────────────────────────────────────────────────────────────────┤
 │                        网络通信层 (network/)                     │
 │   Connection  │  ConnectionManager  │  MessageCycle  │  Socket   │
 ├─────────────────────────────────────────────────────────────────┤
 │                        协议层 (protocol/)                        │
 │   RpcHeader (20B)  │  RpcRequest  │  RpcResponse  │  CRC32      │
 ├─────────────────────────────────────────────────────────────────┤
 │                        服务管理层 (service/)                     │
 │   Service (抽象基类)  │  ServiceManager (单例)                   │
 ├─────────────────────────────────────────────────────────────────┤
 │                        基础设施层                                │
 │   ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────────────┐   │
 │   │ serializer│ │ compress │ │ encrypt  │ │ load_config/log  │   │
 │   └──────────┘ └──────────┘ └──────────┘ └──────────────────┘   │
 │   ┌──────────┐ ┌──────────┐ ┌──────────────────────────────┐    │
 │   │thread_pool│ │ registry │ │ conn_balancer                │    │
 │   └──────────┘ └──────────┘ └──────────────────────────────┘    │
 └─────────────────────────────────────────────────────────────────┘
```

### 数据流：一次 RPC 调用的完整路径

```text
Client                                               Server
──────                                               ──────

call("PingService", "Ping", params)
  │
  ├─ 1. 构造 RpcRequest
  │     {service_name, method_name, timeout_ms, params}
  │
  ├─ 2. 序列化 RpcRequest → Body 字节流
  │
  ├─ 3. 构造 RpcHeader (seq_id=42, body_size, ...)
  │     帧 = 20B Header + Body
  │
  ├─ 4. 写入队列 → async_write ──────────────────────▶  TCP
  │                                                       │
  │                                                    5. Connection::async_read
  │                                                       解析 Header → 读取 Body
  │                                                       │
  │                                                    6. RpcServer::on_message()
  │                                                       │
  │                                                    7. ThreadPool::enqueue()
  │                                                       │
  │                                                    8. process_request():
  │                                                       ├─ 反序列化 RpcRequest
  │                                                       ├─ ServiceManager::dispatch()
  │                                                       │    └─ PingService::handle("Ping", params)
  │                                                       │       → return "pong"
  │                                                       └─ 构造 RpcResponse {0, "", "pong"}
  │                                                       │
  │                                                    9. send_response():
  │                                                       构造 Response 帧 → conn->send()
  │                                                       │
  ◀─── async_read 收到 Response ◀────────────────────  TCP
  │
  ├─ 10. 解析 Header, 读取 Body
  │
  ├─ 11. 反序列化 RpcResponse
  │
  ├─ 12. pending_[42]->promise.set_value(response)
  │
  └─ 13. future.get() → RpcResponse {0, "", "pong"}
```

### 线程模型

```text
服务端:
  main thread               server thread              thread pool (N workers)
  ──────────                ──────────────              ─────────────────────
  信号等待                  io_context.run()            [worker 1] 从队列取任务
  sleep(500ms)              ├─ acceptor 循环              process_request()
  直到 SIGINT               ├─ 每连接读循环                ServiceManager::dispatch()
  server.stop()             └─ 消息回调                    send_response()
                                                      [worker 2] ...
                                                      [worker N] ...

客户端:
  main thread               io_thread
  ──────────                ─────────
  调用 call()                io_context.run()
  等待 future.get()         ├─ 持续读取循环 (read loop)
  处理响应                   │   do_read_header → do_read_body → 匹配 pending → 循环
                            └─ 写入队列处理 (write chain)
```

---

## 目录结构

```text
RPC/
├── src/
│   ├── core/                     # 核心组装层
│   │   ├── rpc_client.h/cpp      #   RPC 客户端（连接、读写循环、写队列）
│   │   ├── rpc_service.h/cpp     #   RPC 服务端（MessageCycle + 线程池 + 分发）
│   │   └── error_code.h          #   统一错误码枚举
│   ├── network/                  # 网络通信层
│   │   ├── connection.h/cpp      #   单连接：异步读写 RPC 帧
│   │   ├── connection_manager.*  #   连接池：线程安全的增删查
│   │   ├── message_cycle.h/cpp   #   事件循环：accept + io_context 驱动
│   │   └── create_socket.h/cpp   #   Socket 创建工具
│   ├── protocol/                 # 协议层
│   │   └── rpc_protocol.h/cpp    #   帧头/请求/响应 序列化 + CRC32
│   ├── service/                  # 服务管理
│   │   ├── service.h             #   Service 抽象基类
│   │   └── service_manager.*     #   服务注册/查找/分发（单例）
│   ├── serializer/               # 序列化
│   │   └── serializer.h          #   Protobuf / JSON 双实现（header-only）
│   ├── compress_data/            # 压缩
│   │   └── compress.h/cpp        #   zstd 压缩/解压
│   ├── encrypt/                  # 加密
│   │   └── encrypt.h/cpp         #   AES-256-GCM 加密/解密
│   ├── thread_pool/              # 线程池
│   │   ├── thread_pool.h/cpp     #   任务队列 + worker 线程
│   │   └── thread_pool_singleton.*#  全局单例线程池
│   ├── registry/                 # 服务注册发现
│   │   ├── service_registry.*    #   ZooKeeper 服务注册
│   │   └── node_manager.*        #   节点管理
│   ├── conn_balancer/            # 负载均衡
│   │   └── conn_balancer.*       #   可插拔均衡策略
│   ├── load_config/              # 配置加载
│   │   ├── load_config.h/cpp     #   JSON 配置解析
│   │   └── log_init.h/cpp        #   日志初始化
│   ├── protos/                   # Protobuf 定义
│   │   ├── message_proto.pb.*    #   生成的 C++ 代码
│   │   └── message_pb.*          #   业务消息封装
│   ├── config/                   # 配置文件
│   │   └── server_config.json
│   ├── server_main.cpp           # 服务端入口
│   └── client_main.cpp           # 客户端入口
├── environment_test/             # 测试文件
│   ├── network_test.cpp          #   网络模块测试 (14 用例)
│   ├── integration_test.cpp      #   完整集成测试 (10 用例)
│   ├── serializer_protocol_test.cpp  # 序列化+协议+服务管理测试
│   ├── compress_encrypt_test.cpp #   压缩+加密测试
│   ├── thread_pool_test.cpp      #   线程池测试
│   └── ...                       #   其他环境验证测试
├── md/                           # 模块设计文档
│   ├── core.md                   #   核心模块文档
│   ├── network.md                #   网络模块文档
│   ├── rpc_protocol.md           #   协议定义文档
│   ├── serializer.md             #   序列化文档
│   ├── service_registration.md   #   服务注册文档
│   ├── thread_pool.md            #   线程池文档
│   ├── compress.md               #   压缩模块文档
│   ├── encrypt.md                #   加密模块文档
│   └── ...                       #   其他模块文档
├── include/                      # 第三方头文件路径
├── CMakeLists.txt                # CMake 构建配置
└── readme.md                     # 本文档
```

---

## 环境与依赖

### 开发环境

| 组件 | 版本/路径 |
| ---- | --------- |
| OS | Windows 11 |
| 工具链 | MSYS2 / MinGW-w64 UCRT64 |
| 编译器 | GCC 15.2.0 (`E:/msys/ucrt64/bin/g++.exe`) |
| 构建系统 | CMake 3.16+ |
| 包管理器 | Vcpkg (`D:/vcpkg-master/vcpkg-master/`) |
| IDE | VSCode |

### 依赖库

| 库 | 用途 |
| -- | ---- |
| Boost 1.90 (Asio) | 异步网络 IO（epoll/IOCP） |
| Protobuf 6.33.4 | 二进制序列化 |
| nlohmann/json 3.12.0 | JSON 解析（配置、序列化） |
| spdlog 1.17.0 | 日志系统 |
| zstd 1.5.7 | 数据压缩 |
| OpenSSL | AES-256-GCM 加密 |
| ZooKeeper C Client 3.9.5 | 服务注册与发现（可选） |

---

## 构建指南

### 1. 配置 CMake

```bash
cd RPC
mkdir -p build && cd build
cmake ..
```

CMake 会自动查找 vcpkg 安装的包。确保 vcpkg 已安装以下包：

- `nlohmann-json_x64-mingw-static`
- `protobuf_x64-mingw-static`
- `zstd_x64-mingw-static`
- `spdlog`（通过 MSYS2）

### 2. 编译所有目标

```bash
cmake --build . -j8
```

### 3. 编译特定目标

```bash
# 仅编译服务端
cmake --build . --target rpc_server -j8

# 仅编译客户端
cmake --build . --target rpc_client -j8

# 编译并运行测试
cmake --build . --target network_test integration_test -j8
```

---

## 运行指南

### 启动服务端

```bash
# 默认配置（监听 0.0.0.0:8080）
./build/rpc_server.exe

# 指定配置文件
./build/rpc_server.exe --config src/config/server_config.json

# 指定端口
./build/rpc_server.exe --port 9090

# 查看帮助
./build/rpc_server.exe --help
```

服务端输出示例：

```text
Loading config: src/config/server_config.json
Server: 0.0.0.0:8080, thread_pool_size=8
Logger initialized
Thread pool created, size=8
Registered PingService (methods: Ping, Stats)

Registered services:
  - PingService

Starting RPC server on 0.0.0.0:8080 ...
Press Ctrl+C to stop.
RPC server listening on port 8080
```

停止：按 `Ctrl+C`，服务端将优雅关闭。

### 运行客户端

```bash
# 默认连接 127.0.0.1:8080
./build/rpc_client.exe

# 指定服务器地址
./build/rpc_client.exe --host 192.168.1.100 --port 9090

# 查看帮助
./build/rpc_client.exe --help
```

客户端输出示例：

```text
=== RPC Client ===
Server: 127.0.0.1:8080
Connecting to 127.0.0.1:8080 ...
Connected!

--- Call PingService::Ping ---
  Result: pong

--- Call PingService::Stats ---
  Result: {"status":"running","connections":1}

--- Call NonExistentService::Foo ---
  Error: [-1] service not found: NonExistentService

--- Performance: 100 Ping calls ---
  Success: 100 / Fail: 0
  Time: 156ms
  QPS: 641

Disconnected.
```

---

## 如何添加新服务

### 1. 定义服务类

继承 `rpc::Service` 基类，实现三个虚函数：

```cpp
// src/my_service.h
#pragma once
#include "service/service.h"

class MyService : public rpc::Service {
public:
    std::string name() const override { return "MyService"; }

    std::vector<std::string> methods() const override {
        return {"Add", "Multiply"};
    }

    std::string handle(const std::string& method,
                       const std::string& params) override {
        if (method == "Add") {
            // 解析 params，执行加法
            return "result_add";
        }
        if (method == "Multiply") {
            // 解析 params，执行乘法
            return "result_multiply";
        }
        throw std::runtime_error("unknown method: " + method);
    }
};
```

### 2. 注册服务

在 `server_main.cpp` 中注册：

```cpp
#include "my_service.h"

// ...

auto my_svc = std::make_shared<MyService>();
server.register_service(my_svc);
```

### 3. 客户端调用

```cpp
// 调用 MyService::Add
auto fut = client.call("MyService", "Add", "{\"a\":1,\"b\":2}");
auto resp = fut.get();
if (resp.error_code == 0) {
    std::cout << "Result: " << resp.result << std::endl;
}
```

---

## 配置文件说明

`src/config/server_config.json`：

```json
{
    "server": {
        "host": "0.0.0.0",
        "port": 8080,
        "thread_pool_size": 8,
        "max_connections": 10000,
        "socket_timeout_ms": 5000
    },
    "log": {
        "dir": "logs",
        "file": "rpc.log",
        "level": "info",
        "max_file_size_mb": 10,
        "max_files": 5
    },
    "serializer": {
        "type": "protobuf"
    },
    "compress": {
        "enable": true,
        "threshold": 256,
        "level": 3
    },
    "encrypt": {
        "enable": false,
        "public_key_path": "keys/public.pem",
        "private_key_path": "keys/private.pem"
    },
    "balancer": {
        "strategy": "round_robin"
    },
    "zk": {
        "host": "127.0.0.1",
        "port": 2181,
        "session_timeout_ms": 4000,
        "root_path": "/rpc"
    }
}
```

配置项说明：

| 字段 | 类型 | 说明 |
| ---- | ---- | ---- |
| `server.host` | string | 监听地址 |
| `server.port` | int | 监听端口 |
| `server.thread_pool_size` | int | 线程池 worker 数量 |
| `server.max_connections` | int | 最大连接数 |
| `server.socket_timeout_ms` | int | Socket 超时（毫秒） |
| `log.dir` | string | 日志目录 |
| `log.file` | string | 日志文件名 |
| `log.level` | string | 日志级别：trace / debug / info / warn / error |
| `log.max_file_size_mb` | int | 单个日志文件最大大小 |
| `log.max_files` | int | 最多保留日志文件数 |
| `serializer.type` | string | 序列化类型：protobuf / json |
| `compress.enable` | bool | 是否启用压缩 |
| `compress.threshold` | int | 压缩阈值（超过此字节数才压缩） |
| `compress.level` | int | zstd 压缩级别（1-19） |
| `encrypt.enable` | bool | 是否启用加密 |
| `encrypt.public_key_path` | string | 公钥文件路径 |
| `encrypt.private_key_path` | string | 私钥文件路径 |
| `balancer.strategy` | string | 负载均衡策略 |
| `zk.host` | string | ZooKeeper 地址 |
| `zk.port` | int | ZooKeeper 端口 |
| `zk.session_timeout_ms` | int | ZK 会话超时 |
| `zk.root_path` | string | ZK 根路径 |

---

## 运行测试

```bash
cd build

# 网络模块测试（14 用例：连接管理、RPC 调用、错误处理）
./network_test.exe

# 完整集成测试（10 用例：线程池、并发调用、断连重连）
./integration_test.exe

# 其他测试
./serializer_protocol_test.exe   # 序列化 + 协议 + 服务管理
./compress_encrypt_test.exe      # 压缩 + 加密
./thread_pool_test.exe           # 线程池
```

预期输出：所有测试 `Passed`，`Failed: 0`。

---

## 模块设计文档

各模块的详细设计文档在 `md/` 目录下：

| 文档 | 内容 |
| ---- | ---- |
| [core.md](md/core.md) | 核心模块：RpcServer / RpcClient 架构、线程模型 |
| [network.md](md/network.md) | 网络层：Connection、MessageCycle、ConnectionManager |
| [rpc_protocol.md](md/rpc_protocol.md) | 协议：帧格式、字段说明、通信流程、CRC32 |
| [serializer.md](md/serializer.md) | 序列化：Protobuf / JSON 实现与切换 |
| [service_registration.md](md/service_registration.md) | 服务注册：Service 基类、ServiceManager 用法 |
| [compress.md](md/compress.md) | 压缩：zstd API 封装 |
| [encrypt.md](md/encrypt.md) | 加密：AES-256-GCM 封装 |
| [thread_pool.md](md/thread_pool.md) | 线程池：任务提交、生命周期 |
| [load_config.md](md/load_config.md) | 配置加载：JSON 解析、结构绑定 |
| [service_registry.md](md/service_registry.md) | 服务发现：ZooKeeper 集成 |
| [conn_balancer.md](md/conn_balancer.md) | 负载均衡：策略模式 |
| [node_manager.md](md/node_manager.md) | 节点管理 |
