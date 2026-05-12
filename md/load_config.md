# 配置加载模块设计文档

## 概述

基于 nlohmann/json 的配置加载模块，覆盖 RPC 框架全部模块参数，支持 JSON 文件读取 + 默认值降级，并提供格式化打印面板。

## 文件位置

| 文件 | 用途 |
|------|------|
| `src/load_config/load_config.h` | 数据结构定义 + API 声明 |
| `src/load_config/load_config.cpp` | JSON 解析 + 格式化打印实现 |
| `src/config/server_config.json` | 默认配置文件 |

---

## 架构设计

### 数据结构层次

```
Config (总配置)
├── ServerConfig     服务器基础（host, port, 线程数, 连接上限）
├── ZkConfig         ZooKeeper 连接（host, port, 路径）
├── LogConfig        日志（目录, 级别, 滚动策略）
├── SerializerConfig 序列化（类型选择）
├── CompressConfig   压缩（开关, 阈值, 级别）
├── EncryptConfig    加密（开关, 密钥路径）
└── BalancerConfig   负载均衡（策略）
```

每个子结构独立定义，便于各模块只依赖自己需要的那部分。

### 调用链路

```
load_config("server_config.json")
    │
    ├── 文件存在？── Yes ──▶ json::parse(ifstream)
    │                            │
    │                   json.contains("server") ?
    │                        │ Yes                │ No
    │                        ▼                    ▼
    │                   parse_server()      保持默认值
    │                        │
    │                        ▼
    │                   ServerConfig{
    │                     host = j.value("host", "0.0.0.0")
    │                     port = j.value("port", 8080)
    │                     ...
    │                   }
    │                        │
    │                   (同理 zk, log, ...)
    │                        │
    └── 无文件 ──────────────▶ 全部默认值
                                    │
                                    ▼
                              return Config{}
```

---

## API 设计

### 加载函数

```cpp
Config rpc::load_config(const std::string& filepath);
```

- 文件存在且 JSON 合法 → 覆盖默认值
- 文件不存在 → 全部默认值，**不报错**
- JSON 格式错误 → 打印错误信息，**全部默认值**

`json::value(key, default)` 保证即使 JSON 缺少某个字段也不会崩溃。

### 打印函数

```cpp
void rpc::print_config(const Config& cfg);
```

以表格面板格式输出全部配置项，用于启动时诊断：

```
┌─────────────────────────────────────────┐
│  RPC Server Configuration              │
├─────────────────────────────────────────┤
  ▶ Server
    host                    0.0.0.0
    port                    8080
    thread_pool_size        8
    ...
```

---

## 默认配置文件

位置 `src/config/server_config.json`，JSON 结构按模块分节：

```json
{
    "server":   { ... },
    "zk":       { ... },
    "log":      { ... },
    "serializer": { ... },
    "compress": { ... },
    "encrypt":  { ... },
    "balancer": { ... }
}
```

每节均可省略，省略时该模块全部取默认值。

---

## 默认值策略

| 参数 | 默认值 | 理由 |
|------|--------|------|
| `server.host` | `0.0.0.0` | 监听所有网卡 |
| `server.port` | `8080` | 常用开发端口 |
| `server.thread_pool_size` | `8` | 适合 4 核超线程 |
| `server.max_connections` | `10000` | 单机合理上限 |
| `zk.host` | `127.0.0.1:2181` | 本地 ZK 默认地址 |
| `zk.session_timeout_ms` | `4000` | ZK 默认超时 |
| `log.level` | `info` | 开发足够，生产可用 warn |
| `log.max_file_size_mb` | `10` × `5` | 50MB 上限，控制磁盘占用 |
| `serializer.type` | `protobuf` | 高性能首选 |
| `compress.enable` | `true` | 默认开启，阈值 256 字节 |
| `compress.level` | `3` | zstd 平衡级 |
| `encrypt.enable` | `false` | 内网默认不加密 |
| `balancer.strategy` | `round_robin` | 均衡分发 |

---

## 集成方式

每个模块获取自己的配置子结构：

```cpp
// server_main.cpp 启动时
auto cfg = rpc::load_config("src/config/server_config.json");
rpc::print_config(cfg);  // 启动诊断

// 各模块初始化时接收自己需要的部分
void NetworkModule::init(const ServerConfig& server, const ZkConfig& zk) {
    listen(server.host, server.port);
    connect_zk(zk.host, zk.port);
}
```

子结构传值而非传引用，各模块不依赖全局 Config，降低耦合。

---

## 编译依赖

- **nlohmann/json 3.12.0**（vcpkg，header-only）
- **C++17**（`<filesystem>`）
- `#include <nlohmann/json.hpp>` + 链接 `nlohmann_json::nlohmann_json`

---

## 使用示例

### 基础用法

```cpp
#include "load_config/load_config.h"
#include <iostream>

int main() {
    auto cfg = rpc::load_config("src/config/server_config.json");
    rpc::print_config(cfg);

    // 直接访问任意配置项
    std::cout << "Server listening on " << cfg.server.host
              << ":" << cfg.server.port << '\n';
}
```

### 运行时覆盖

```cpp
auto cfg = rpc::load_config("config.json");

// 命令行参数覆盖
if (args.has("--port")) {
    cfg.server.port = args.get<int>("--port");
}

// 环境变量覆盖
if (auto* env = std::getenv("RPC_LOG_LEVEL")) {
    cfg.log.level = env;
}
```
