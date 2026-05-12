# RPC 协议定义文档

## 概述

RPC 协议定义了客户端与服务端之间的通信格式。协议采用 TLV（Type-Length-Value）风格的二进制帧格式，包含固定大小的帧头和变长的消息体，支持请求/响应匹配、序列化器协商、压缩加密标志和 CRC 校验。

## 文件位置

| 文件 | 用途 |
|------|------|
| `src/protocol/rpc_protocol.h` | 协议结构定义 + 消息类型枚举 |
| `src/protocol/rpc_protocol.cpp` | 序列化/反序列化实现 + CRC32 |

---

## 协议帧格式

### 整体结构

```
  0                   1                   2                   3
  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 ┌───────────────────────────────────────────────────────────────┐
 │                            magic                              │  ← 4 bytes
 │                         0x52504301                            │
 ├──────────┬──────────┬──────────┬──────────┬──────────────────┤
 │ version  │ msg_type │serializer│  flags   │     seq_id       │  ← 1+1+1+1+4 bytes
 ├──────────┴──────────┴──────────┴──────────┴──────────────────┤
 │                           body_size                           │  ← 4 bytes
 ├───────────────────────────────────────────────────────────────┤
 │                           checksum                            │  ← 4 bytes
 ├───────────────────────────────────────────────────────────────┤
 │                                                               │
 │                     Body (变长, body_size bytes)               │
 │          RpcRequest 或 RpcResponse 的序列化数据               │
 │                                                               │
 └───────────────────────────────────────────────────────────────┘
```

**帧头总大小：20 字节（固定）**

### 帧头字段说明

| 字段 | 偏移 | 大小 | 类型 | 说明 |
|------|------|------|------|------|
| `magic` | 0 | 4 | uint32_t | 魔术字，固定为 `0x52504301`（"RPC\x01"），用于快速识别 RPC 协议帧 |
| `version` | 4 | 1 | uint8_t | 协议版本号，当前为 `1`，用于后续兼容性升级 |
| `msg_type` | 5 | 1 | uint8_t | 消息类型，对应 `MsgType` 枚举 |
| `serializer` | 6 | 1 | uint8_t | 序列化器类型，对应 `SerType` 枚举，用于服务端选择解序列化方式 |
| `flags` | 7 | 1 | uint8_t | 标志位（见下方 `RpcFlags`） |
| `seq_id` | 8 | 4 | uint32_t | 请求序列号，由客户端生成递增，用于将 Response 匹配到对应的 Request |
| `body_size` | 12 | 4 | uint32_t | Body 的长度（字节），纯 Body 不含帧头 |
| `checksum` | 16 | 4 | uint32_t | Body 的 CRC32 校验值，0 表示不启用校验 |

**所有多字节字段使用网络字节序（大端）。**

---

## 枚举定义

### MsgType — 消息类型

| 值 | 枚举 | 说明 |
|----|------|------|
| `0x01` | `REQUEST` | 客户端 → 服务端：发起 RPC 调用 |
| `0x02` | `RESPONSE` | 服务端 → 客户端：RPC 调用成功返回 |
| `0x03` | `RPC_ERROR` | 服务端 → 客户端：RPC 调用失败（服务/方法不存在、超时等） |
| `0x04` | `HEARTBEAT` | 心跳包，用于连接保活和健康检测 |

### SerType — 序列化器标识

| 值 | 枚举 | 说明 |
|----|------|------|
| `0` | `PROTOBUF` | Protobuf 二进制序列化 |
| `1` | `JSON` | JSON 文本序列化 |

### RpcFlags — 标志位

| 位 | 值 | 说明 |
|----|----|------|
| bit 0 | `0x01` | `FLAG_COMPRESS` — Body 已使用 zstd 压缩 |
| bit 1 | `0x02` | `FLAG_ENCRYPT` — Body 已使用 AES-256-GCM 加密 |

标志位的处理顺序：**先解密 → 再解压 → 再反序列化**。

---

## Request 请求体

### 结构（C++）

```cpp
struct RpcRequest {
    std::string service_name;  // 目标服务名，如 "UserService"
    std::string method_name;   // 目标方法名，如 "GetUser"
    uint32_t    timeout_ms;    // 超时时间（毫秒），0 表示无超时
    std::string params;        // 序列化后的参数（由 serializer 序列化）
};
```

### Wire Format

```
┌─────────────────────┬──────────────────────────────────────────┐
│ service_name_len    │ uint16_t, 大端                           │
├─────────────────────┼──────────────────────────────────────────┤
│ service_name        │ N 字节                                   │
├─────────────────────┼──────────────────────────────────────────┤
│ method_name_len     │ uint16_t, 大端                           │
├─────────────────────┼──────────────────────────────────────────┤
│ method_name         │ N 字节                                   │
├─────────────────────┼──────────────────────────────────────────┤
│ timeout_ms          │ uint32_t, 大端                           │
├─────────────────────┼──────────────────────────────────────────┤
│ params_len          │ uint32_t, 大端                           │
├─────────────────────┼──────────────────────────────────────────┤
│ params              │ N 字节（序列化后的 protobuf 参数）       │
└─────────────────────┴──────────────────────────────────────────┘
```

### 各字段说明

| 字段 | 说明 |
|------|------|
| `service_name` | 目标 RPC 服务名，服务端通过 `ServiceManager` 查找对应服务 |
| `method_name` | 目标方法名，服务端通过服务对象的 `handle(method, params)` 分发 |
| `timeout_ms` | 调用超时时间，客户端等待的最大时间（毫秒），超时则返回超时错误 |
| `params` | 方法参数，已由指定的 serializer 序列化为字节流 |

---

## Response 响应体

### 结构（C++）

```cpp
struct RpcResponse {
    int32_t     error_code;  // 0 = 成功，非 0 = 错误码
    std::string error_msg;   // 错误描述（成功时为空）
    std::string result;      // 序列化后的返回值（成功时有内容）
};
```

### Wire Format

```
┌─────────────────────┬──────────────────────────────────────────┐
│ error_code          │ int32_t, 大端                            │
├─────────────────────┼──────────────────────────────────────────┤
│ error_msg_len       │ uint16_t, 大端                           │
├─────────────────────┼──────────────────────────────────────────┤
│ error_msg           │ N 字节（成功时为空）                     │
├─────────────────────┼──────────────────────────────────────────┤
│ result_len          │ uint32_t, 大端                           │
├─────────────────────┼──────────────────────────────────────────┤
│ result              │ N 字节（序列化后的返回值）               │
└─────────────────────┴──────────────────────────────────────────┘
```

### 各字段说明

| 字段 | 说明 |
|------|------|
| `error_code` | 错误码：0 表示成功，非 0 表示失败（负数表示服务端内部错误，正数可自定义） |
| `error_msg` | 错误描述文本，仅 error_code != 0 时有内容，用于日志/调试 |
| `result` | 方法的返回值，已由指定的 serializer 序列化为字节流，error_code != 0 时为空 |

---

## 通信流程示意

### 正常调用

```
Client                              Server
  │                                    │
  │ ── REQUEST (seq_id=1) ──────────▶  │
  │    service="UserService"           │
  │    method="GetUser"                │
  │    params=<serialized>             │
  │                                    │  ServiceManager::dispatch()
  │                                    │  service->handle(method, params)
  │  ◀── RESPONSE (seq_id=1) ────────  │
  │    error_code=0                    │
  │    result=<serialized>             │
  │                                    │
```

### 错误调用

```
Client                              Server
  │                                    │
  │ ── REQUEST (seq_id=2) ──────────▶  │
  │    service="NotExist"              │
  │                                    │  ServiceManager::dispatch()
  │                                    │  → service not found
  │  ◀── ERROR (seq_id=2) ─────────── │
  │    error_code=-1                   │
  │    error_msg="service not found"   │
  │                                    │
```

### 带压缩/加密的调用

```
Client                              Server
  │                                    │
  │  1. 序列化参数                     │
  │  2. 压缩 body                      │
  │  3. 加密 body                      │
  │  4. 设置 FLAG_COMPRESS|FLAG_ENCRYPT│
  │ ── REQUEST ──────────────────────▶ │
  │                                    │  1. 检查 flags
  │                                    │  2. 解密 body
  │                                    │  3. 解压 body
  │                                    │  4. 反序列化参数
  │                                    │  5. dispatch()
  │                                    │
```

---

## CRC32 校验

- 算法：ISO-HDLC CRC32（多项式 `0xEDB88320`），与 gzip/PNG 相同
- 计算方式：对 Body 字段计算 CRC32，存入帧头的 `checksum` 字段
- 不启用校验：`checksum = 0`
- 只校验 Body 部分，帧头本身不校验

---

## 测试结果

| 测试 | 内容 | 结果 |
|------|------|------|
| header roundtrip | 帧头大端序列化/反序列化 | OK |
| header too short | 数据不足抛异常 | OK |
| magic validation | 魔术字正确 | OK |
| request roundtrip | 含所有字段的请求往返 | OK |
| request empty | 空字段请求 | OK |
| request binary | 含 \x00 的二进制参数 | OK |
| response success | 成功响应往返 | OK |
| response error | 错误响应往返 | OK |
| response truncated | 截断数据抛异常 | OK |
| crc32 deterministic | 相同输入相同输出 | OK |
| crc32 different | 不同输入不同输出 | OK |
| crc32 empty | 空字符串 CRC=0 | OK |
| MsgType values | 枚举值正确 | OK |
| RpcFlags values | 标志位值正确 | OK |

---

## 编译依赖

- 纯标准库 `std::string` / `std::vector`，无外部依赖
- `rpc_protocol.cpp` 不依赖 protobuf 或任何第三方库
- 字节序转换使用手动大端编码（不依赖 `<arpa/inet.h>`）
