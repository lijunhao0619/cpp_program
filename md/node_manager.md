# ZK 节点管理模块设计文档

## 概述

整合 `ServiceRegistry`（ZK 通信）与 `LoadBalancer`（策略选择），提供统一的 ZK 节点管理 API：注册、发现、列表、负载均衡选择一站式接口。

## 文件位置

| 文件 | 用途 |
|------|------|
| `src/registry/node_manager.h` | `NodeManager` 类声明 |
| `src/registry/node_manager.cpp` | 实现：整合 registry + balancer |

## 架构设计

### 模块组合

```
┌──────────────────────────────────────────┐
│              NodeManager                  │
│  ┌──────────────┐  ┌──────────────────┐  │
│  │ServiceRegistry│  │  LoadBalancer   │  │
│  │              │  │                  │  │
│  │ get_children │──▶ set_nodes(eps)  │  │
│  │ get_data     │  │ set_strategy()   │  │
│  │ create node  │  │ select()         │  │
│  │ delete node  │  │ add/remove node  │  │
│  └──────┬───────┘  └────────┬─────────┘  │
│         │                   │             │
└─────────┼───────────────────┼─────────────┘
          │                   │
          ▼                   ▼
    ZooKeeper           返回选择的
    Server              Endpoint
```

### 调用链路

```
NodeManager::select_node()
    │
    ├── nodes_ 为空？
    │       Yes ──▶ refresh_nodes()
    │                   │
    │                   ▼
    │              registry_->list_services()
    │                   │
    │                   ▼
    │              balancer_.set_nodes(nodes_)
    │
    ▼
    balancer_.select() → Endpoint
```

首次调用 `select_node()` 时自动从 ZK 刷新节点列表，后续调用使用缓存。`refresh_nodes()` 显式刷新缓存。

---

## API 设计

### 连接与初始化

```cpp
bool connect(const std::string& host, uint16_t port, int32_t timeout_ms);
void init_zk_paths(const std::string& services_path);
```

`init_zk_paths` 确保 `/rpc` 和 `/rpc/services` 持久节点存在。

### 节点发现

```cpp
// 刷新缓存（从 ZK 拉取最新列表）
std::vector<Endpoint> refresh_nodes();

// 获取缓存的节点列表
const std::vector<Endpoint>& nodes() const;

// 列出 ZK 子节点名（不解析数据）
std::vector<std::string> list_node_names();

// 获取某节点的数据
std::string get_node_data(const std::string& path);
```

### 负载均衡选择

```cpp
// 使用当前策略选择一个节点（自动刷新）
Endpoint select_node();

// 切换策略
void set_balancer_strategy(const std::string& name);

// 当前策略
std::string strategy_name() const;
```

### 节点注册与注销

```cpp
bool register_node(const std::string& node_name,
                   const std::string& address,
                   int weight = 1);

bool unregister_node(const std::string& node_name);
bool node_exists(const std::string& node_path);
```

---

## 实现要点

### 1. 懒加载刷新

```cpp
Endpoint NodeManager::select_node() {
    if (nodes_.empty()) {
        refresh_nodes();  // 首次调用自动从 ZK 拉取
    }
    return balancer_.select();
}
```

避免空节点列表时抛出异常。

### 2. 策略切换时重新设置节点

```cpp
void NodeManager::set_balancer_strategy(const std::string& name) {
    balancer_.set_strategy(create_strategy(name));
    balancer_.set_nodes(nodes_); // 同步到新策略
}
```

策略对象内部计数器被重置，避免旧状态干扰。

### 3. ZK 路径初始化

```cpp
void NodeManager::init_zk_paths(const std::string& path) {
    // /rpc/services → 先创建 /rpc，再创建 /rpc/services
    registry_->create_persistent_node("/rpc");
    registry_->create_persistent_node("/rpc/services");
}
```

已存在节点不报错（ServiceRegistry 内部静默处理）。

---

## 使用示例

### 服务端启动

```cpp
boost::asio::io_context ios;
rpc::NodeManager mgr(ios);

mgr.connect("127.0.0.1", 2181);
mgr.init_zk_paths("/rpc/services");
mgr.register_node("my_svc_1", "192.168.1.100:8080");
// 服务退出时 ZK 自动清理（临时节点）
```

### 客户端调用

```cpp
boost::asio::io_context ios;
rpc::NodeManager mgr(ios);

mgr.connect("127.0.0.1", 2181);
mgr.set_balancer_strategy("round_robin");

// 每次调用前可选择刷新
mgr.refresh_nodes();

// 获取负载均衡后的目标
rpc::Endpoint target = mgr.select_node();
std::cout << "Calling " << target.name << " @ " << target.address;

// 也可直接遍历所有节点
for (auto& node : mgr.nodes()) {
    // ...
}
```

### 完整流程

```cpp
// 服务端 A
NodeManager mgr_a(ios_a);
mgr_a.connect();
mgr_a.init_zk_paths();
mgr_a.register_node("srv_a", "10.0.0.1:8080");

// 服务端 B
NodeManager mgr_b(ios_b);
mgr_b.connect();
mgr_b.init_zk_paths();
mgr_b.register_node("srv_b", "10.0.0.2:8080");

// 客户端
NodeManager mgr_c(ios_c);
mgr_c.connect();
mgr_c.set_balancer_strategy("round_robin");
mgr_c.refresh_nodes();

// 连续调用：A → B → A → B ...
for (int i = 0; i < 10; ++i) {
    auto ep = mgr_c.select_node();
    // call ep.address
}
```

## 依赖

| 模块 | 依赖关系 |
|------|----------|
| `NodeManager` | `ServiceRegistry` + `LoadBalancer` |
| `ServiceRegistry` | `zk_client` + `boost::asio` |
| `LoadBalancer` | 纯标准库 |
