# ZooKeeper 服务注册与发现模块设计文档

## 概述

封装自研 `zk_client` (Boost.Asio ZK wire protocol) 为上层服务注册发现 API，提供连接管理、节点 CRUD、服务注册/注销/列表功能，使用 `std::future` + `io_context::poll_one()` 的混合模式简化同步调用。

## 文件位置

| 文件 | 用途 |
|------|------|
| `src/registry/service_registry.h` | API 声明 + `ServiceInstance` 结构 |
| `src/registry/service_registry.cpp` | 实现：ZK 操作封装 |
| `include/zk_client/zk_client.hpp` | 底层 ZK wire protocol 客户端 |

---

## 架构设计

### 分层关系

```
┌─────────────────────────────┐
│   ServiceRegistry           │  ← 对外 API（同步风格）
│   (service_registry.h/cpp)  │
├─────────────────────────────┤
│   zk_client                 │  ← ZK wire protocol 实现
│   (include/zk_client/)      │     Boost.Asio 异步 TCP
├─────────────────────────────┤
│   ZooKeeper Server          │  ← 外部 ZK 服务
│   (localhost:2181)          │
└─────────────────────────────┘
```

### 同步化策略

`zk_client` 所有操作返回 `std::future<result<T>>`，内部依赖 `io_context` 推进异步回调。`ServiceRegistry` 通过 **poll-wait 循环** 将异步转为同步：

```
register_service(path, name, addr)
    │
    ▼
client_->create(...)                     // 返回 future
    │
    ▼
while (future.wait_for(10ms) == timeout) // 等待完成
    ios_.poll_one();                     // 推进一个 handler
    │
    ▼
future.get() → result                    // 结果
```

此设计让调用方无需管理 io_context 线程，而底层始终保持纯异步实现。

---

## API 设计

### ServiceInstance

```cpp
struct ServiceInstance {
    std::string name;        // ZK 节点名 (如 "order_service_1")
    std::string address;     // IP:Port (如 "192.168.1.100:8080")
    int64_t     ctime;       // ZK 创建时间
    int64_t     mtime;       // ZK 修改时间
    int32_t     version;     // ZK 节点版本
    int32_t     data_length; // 数据字节数
};
```

### 连接管理

```cpp
bool connect(const std::string& host = "127.0.0.1",
             uint16_t port = 2181,
             int32_t timeout_ms = 4000);
```

返回 `false` 时连接失败，调用方应退出或重试。

### 服务发现

```cpp
// 列出路径下所有子节点名
std::vector<std::string> get_children(const std::string& path);

// 列出所有已注册服务（从节点名+数据中解析）
std::vector<ServiceInstance> list_services(
    const std::string& services_path = "/rpc/services");

// 读取单个节点的文本数据
std::string get_node_data(const std::string& path);

// 获取某个服务实例的完整信息
ServiceInstance get_service(const std::string& node_path);

// 判断节点是否存在
bool node_exists(const std::string& path);
```

### 服务注册/注销

```cpp
// 注册服务：在 services_path 下创建临时节点，数据为 address
// flags=1 (ephemeral)，连接断开后 ZK 自动清理
bool register_service(const std::string& services_path,
                      const std::string& node_name,
                      const std::string& address);

// 注销服务：删除节点
bool unregister_service(const std::string& node_path);

// 创建持久节点（用于框架初始化 /rpc、/rpc/services）
bool create_persistent_node(const std::string& path);
```

### 节点类型对比

| 节点类型 | ZK flags | 生命周期 |
|----------|----------|----------|
| 持久节点 | `0` | 显式删除前一直存在 |
| 临时节点 | `1` | 客户端会话断开后 ZK 自动删除 |
| 持久顺序 | `2` | 持久 + ZK 自动追加 10 位序号 |
| 临时顺序 | `3` | 临时 + ZK 自动追加 10 位序号 |

服务实例使用 **临时节点** (`flags=1`)：服务端崩溃/断连后 ZK 自动清理注册信息，避免调用方路由到已失效的节点。

---

## 集成方式

### 启动时初始化 ZK 节点结构

```cpp
registry.create_persistent_node("/rpc");
registry.create_persistent_node("/rpc/services");
```

已存在的节点不会报错（自动忽略 "node exists" 错误）。

### 服务端注册

```cpp
ServiceRegistry registry(ios);
registry.connect("127.0.0.1", 2181);
registry.register_service("/rpc/services", "order_svc",
                          "192.168.1.10:8080");
// 服务端退出时 ZK 自动清理（临时节点）
```

### 客户端发现

```cpp
ServiceRegistry registry(ios);
registry.connect("127.0.0.1", 2181);

for (auto& svc : registry.list_services("/rpc/services")) {
    std::cout << svc.name << " @ " << svc.address << "\n";
}
```

---

## 使用示例

### 完整注册/发现/注销流程

```cpp
boost::asio::io_context ios;
rpc::ServiceRegistry registry(ios);

// 1. 连接
registry.connect("127.0.0.1", 2181);

// 2. 初始化路径
registry.create_persistent_node("/rpc/services");

// 3. 注册服务
registry.register_service("/rpc/services", "calc_1",
                          "10.0.0.5:9000");

// 4. 客户端发现
for (auto& svc : registry.list_services("/rpc/services")) {
    // use svc.name, svc.address ...
}

// 5. 检查是否存在
if (registry.node_exists("/rpc/services/calc_1")) {
    // 节点存在时的处理
}

// 6. 读取节点数据
std::string addr = registry.get_node_data("/rpc/services/calc_1");

// 7. 注销
registry.unregister_service("/rpc/services/calc_1");
```

---

## 编译依赖

- **zk_client** (自研模块，`include/zk_client/`)
- **Boost.Asio** (`boost/asio.hpp`，header-only + ws2_32)
- **C++17** (`<string_view>`, `std::future`)
- 链接：`-lws2_32`
