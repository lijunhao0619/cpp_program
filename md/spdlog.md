# 日志模块设计文档

## 概述

基于 spdlog 的 RPC 日志模块，提供初始化与便捷宏定义，支持控制台+文件双输出，线程安全。

## 文件位置

| 文件 | 用途 |
|------|------|
| `src/load_config/log_init.h` | 头文件：API 声明 + 宏定义 |
| `src/load_config/log_init.cpp` | 实现：初始化逻辑 |

## 设计目标

1. **零配置启动**：首次调用宏时自动初始化默认配置，无需显式 `init_logger()`
2. **双 sink 输出**：控制台彩色（开发调试）+ 文件滚动（生产留存）
3. **宏封装**：隐藏 `get_logger()` 调用，自动捕获源码位置
4. **线程安全**：基于 spdlog 内置线程安全 + `std::call_once` 防重复初始化
5. **运行时可调**：日志级别可在运行中动态修改，无需重启

---

## 架构设计

### 调用链路

```
RPC_INFO("msg {}", arg)
    │
    ▼
::rpc::get_logger()->info(...)     // 宏展开
    │
    ▼
get_logger() 首次调用 → std::call_once → init_logger()
    │                                       │
    │                         ┌─────────────┴─────────────┐
    │                         ▼                           ▼
    │                   console_sink               rotating_file_sink
    │                   (彩色控制台)               (10MB×5 滚动)
    │                         │                           │
    │                         └──────────┬────────────────┘
    │                                    ▼
    └────────────── 返回 g_logger ──▶ spdlog::logger("rpc")
```

### Sink 策略选择

| Sink | 用途 | 级别过滤 | 生命周期 |
|------|------|----------|----------|
| `stdout_color_sink_mt` | 开发调试，终端实时输出 | 可配 | 程序退出时 spdlog 自动清理 |
| `rotating_file_sink_mt` | 生产日志留存 | 可配 | 同上，10MB/文件 × 5 = 50MB 上限 |

选择 `rotating_file_sink` 而非 `daily_file_sink` 的原因：RPC 框架流量不均，按大小滚动更可控，避免单文件过大。

---

## API 设计

### 初始化函数

```cpp
void rpc::init_logger(
    const std::string& log_dir  = "logs",        // 日志目录
    const std::string& log_file = "rpc.log",     // 文件名
    spdlog::level::level_enum level = info);     // 最低输出级别
```

- 内部使用 `std::call_once`，多次调用安全，仅首次生效
- 未调用时，`get_logger()` 自动以默认参数初始化

### Logger 获取

```cpp
std::shared_ptr<spdlog::logger> rpc::get_logger();
```

返回全局唯一 logger 实例，首次调用触发懒初始化。

### 运行时调整

```cpp
void rpc::set_log_level(spdlog::level::level_enum level);
```

开发时可用 `set_log_level(trace)` 开启详细日志；生产用 `set_log_level(warn)` 减少输出。

---

## 宏定义设计

### 六个标准级别宏

```cpp
RPC_TRACE("detailed trace: {}", var)
RPC_DEBUG("debug value: {}", x)
RPC_INFO ("connection established: {}:{}", host, port)
RPC_WARN ("retry attempt {}/{}", n, max)
RPC_ERROR("request failed: code={}", code)
RPC_CRITICAL("server crash: {}", what)
```

### 设计要点

| 特性 | 实现方式 |
|------|----------|
| 零参数传递 | 宏直接调用 `::rpc::get_logger()`，无需传 logger 对象 |
| 命名空间保护 | 使用 `::rpc::` 全局限定，避免宏在任意作用域下歧义 |
| 源码位置 | spdlog 的 `logger::info()` 自动通过 `__builtin_FILE()` / `__builtin_LINE()` 捕获位置（开启 `-DSPDLOG_ACTIVE_LEVEL` 时） |
| 性能检查 | `RPC_LOG_TRACE_ENABLED` / `RPC_LOG_DEBUG_ENABLED` 宏，避免 `trace` 级别下的无效格式化 |

### 带条件日志的使用模式

```cpp
// 避免在 TRACE 关闭时仍执行昂贵的格式化
if (RPC_LOG_TRACE_ENABLED) {
    std::string dump = serialize_large_object(obj);  // 昂贵操作
    RPC_TRACE("object: {}", dump);
}
```

---

## 实现要点

### 1. 单次初始化（std::call_once）

```cpp
static std::once_flag g_init_flag;
std::call_once(g_init_flag, [&] { /* ... */ });
```

`init_logger()` 可能被多个线程并发调用（如多个网络线程启动时），`call_once` 保证只执行一次，避免多个 logger 实例。

### 2. 懒初始化（get_logger）

```cpp
if (!g_logger) {
    init_logger();  // 使用默认参数
}
return g_logger;
```

首次调用日志宏时自动初始化，免去手动调用 `init_logger()` 的心智负担。

### 3. 日志格式

```
[2026-05-07 21:30:45.123] [info] [12345] Server started on port 8080
 ──────────┬────────────  ──┬──  ──┬──  ───────────┬──────────────
       日期+时间           级别   线程ID        消息体
```

选择 `[线程ID]` 而非 `[logger名]`：RPC 框架多线程请求处理，线程 ID 比 logger 名更有诊断价值。

### 4. Free-Store 分配

使用 `std::make_shared<spdlog::logger>(...)` 而非 `spdlog::stdout_color_mt("name")` 工厂函数，因为：
- 手动组装 sink 列表更灵活
- 避免工厂函数自动注册到全局注册表带来的名字冲突

### 5. flush 策略

`flush_on(spdlog::level::err)` — 仅 error/critical 自动 flush。info 级别高频日志依赖系统缓冲，减少磁盘 IO。

---

## 使用示例

### 最小用法

```cpp
#include "src/load_config/log_init.h"

int main() {
    // 无需显式初始化，直接使用宏
    RPC_INFO("Server starting...");
    RPC_WARN("Config file not found, using defaults");
    // ...
    RPC_INFO("Server stopped");
}
```

### 自定义初始化

```cpp
#include "src/load_config/log_init.h"

int main() {
    // 显式初始化：指定目录、文件名、级别
    rpc::init_logger("logs/prod", "server.log", spdlog::level::warn);

    RPC_DEBUG("不会输出");  // debug < warn
    RPC_WARN("这条会输出");
    RPC_ERROR("这条会输出并自动 flush 到磁盘");
}
```

### 多模块使用

```cpp
// network/connection.cpp
#include "src/load_config/log_init.h"

void Connection::handle_read() {
    RPC_DEBUG("read {} bytes from fd={}", bytes, fd_);
    if (error) {
        RPC_ERROR("read error: {}", ec.message());
    }
}
```

宏展开后等价于：

```cpp
::rpc::get_logger()->debug("read {} bytes from fd={}", bytes, fd_);
```

始终从全局 logger 输出，无需层层传递 logger 对象。

---

## 依赖与编译

- **spdlog 1.17.0**（MSYS2 系统包）
- **C++20**（`<filesystem>` 用于创建日志目录）
- CMake 已链接 `spdlog::spdlog`，无需额外配置
