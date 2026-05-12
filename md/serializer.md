# 序列化模块设计文档

## 概述

RPC 序列化模块负责将 C++ 对象转换为网络传输的字节流（序列化），以及从字节流还原为 C++ 对象（反序列化）。模块使用策略模式，支持多种序列化方式，当前实现 Protobuf 二进制和 JSON 两种。

## 文件位置

| 文件 | 用途 |
|------|------|
| `src/serializer/serializer.h` | 抽象接口 + Protobuf/JSON 实现 + 工厂函数 |

模块为 header-only，所有实现均在头文件中。

---

## 架构设计

```
                    ┌──────────────────┐
                    │    Serializer    │  ← 抽象接口
                    ├──────────────────┤
                    │ + name()         │
                    │ + type()         │
                    │ + serialize()    │
                    │ + deserialize()  │
                    └────────┬─────────┘
                             │
              ┌──────────────┴──────────────┐
              ▼                              ▼
   ┌─────────────────────┐    ┌─────────────────────┐
   │ ProtobufSerializer  │    │   JsonSerializer    │
   ├─────────────────────┤    ├─────────────────────┤
   │ SerializeToString() │    │ MessageToJsonString │
   │ ParseFromString()   │    │ JsonStringToMessage │
   └─────────────────────┘    └─────────────────────┘
```

### 序列化流程

```
用户数据 (protobuf::Message)
        │
        ▼
Serializer::serialize(msg)
        │
        ├── ProtobufSerializer → msg.SerializeToString() → 紧凑二进制
        │
        └── JsonSerializer → MessageToJsonString(msg) → 可读 JSON
                                │
                                ▼
                        字节流 (std::string)
                        通过网络传输
```

### 反序列化流程

```
字节流 (std::string)
        │
        ▼
Serializer::deserialize(data, msg)
        │
        ├── ProtobufSerializer → msg.ParseFromString(data) → bool
        │
        └── JsonSerializer → JsonStringToMessage(data, &msg) → bool
                                │
                                ▼
                        还原的 C++ 对象
```

---

## API 设计

### 枚举定义

```cpp
// 序列化器类型标识
enum class SerializerType : uint8_t {
    PROTOBUF = 0,  // Protobuf 二进制
    JSON     = 1,  // JSON 文本
};
```

### 抽象接口

```cpp
class Serializer {
public:
    virtual ~Serializer() = default;

    // 序列化器名称（"protobuf" / "json"）
    virtual std::string name() const = 0;

    // 序列化类型枚举
    virtual SerializerType type() const = 0;

    // 序列化：protobuf Message → 字节流
    virtual std::string serialize(const google::protobuf::Message& msg) = 0;

    // 反序列化：字节流 → protobuf Message
    // 成功返回 true，失败返回 false
    virtual bool deserialize(const std::string& data,
                             google::protobuf::Message& msg) = 0;
};
```

### 工厂函数

```cpp
// 通过枚举创建
std::unique_ptr<Serializer> create_serializer(SerializerType type);

// 通过名称创建（支持 "protobuf" / "proto" / "json"）
std::unique_ptr<Serializer> create_serializer(const std::string& name);
```

---

## 两种序列化器对比

| 特性 | ProtobufSerializer | JsonSerializer |
|------|-------------------|----------------|
| 格式 | 紧凑二进制 | 可读文本 |
| 体积 | 小（通常 3-10x 压缩） | 较大 |
| 速度 | 快 | 较慢 |
| 可读性 | 不可读 | 可读 |
| 调试 | 需工具 | 肉眼可读 |
| 适用场景 | 生产环境 RPC | 调试、REST API、跨语言 |

### ProtobufSerializer

- 使用 `google::protobuf::Message::SerializeToString()` 序列化
- 使用 `google::protobuf::Message::ParseFromString()` 反序列化
- 紧凑二进制格式，性能最优

### JsonSerializer

- 使用 `google::protobuf::util::MessageToJsonString()` 序列化
- 使用 `google::protobuf::util::JsonStringToMessage()` 反序列化
- 输出标准 proto3 JSON 格式（camelCase 字段名）
- 反序列化失败返回 false（不抛异常）

---

## 使用示例

```cpp
#include "serializer/serializer.h"

// 定义 proto 消息
// message User {
//     int32  id    = 1;
//     string name  = 2;
//     string email = 3;
// }

// ── Protobuf 二进制序列化 ──
auto pb_ser = rpc::create_serializer(rpc::SerializerType::PROTOBUF);
// 或: auto pb_ser = rpc::create_serializer("protobuf");

User user;
user.set_id(42);
user.set_name("Alice");

std::string binary_data = pb_ser->serialize(user);
// binary_data 是紧凑的 protobuf 二进制

User decoded;
if (pb_ser->deserialize(binary_data, decoded)) {
    assert(decoded.id() == 42);
}

// ── JSON 序列化 ──
auto json_ser = rpc::create_serializer(rpc::SerializerType::JSON);
// 或: auto json_ser = rpc::create_serializer("json");

std::string json_data = json_ser->serialize(user);
// json_data: {"id":42,"name":"Alice"}

User decoded2;
if (json_ser->deserialize(json_data, decoded2)) {
    assert(decoded2.id() == 42);
}
```

### RPC 集成

```cpp
// 通过配置选择序列化器
auto serializer = rpc::create_serializer(config.serializer_type);

// 客户端：序列化请求参数
GetUserRequest params;
params.set_user_id(100);
std::string body = serializer->serialize(params);
// body 通过网络发送...

// 服务端：反序列化请求参数
GetUserRequest received;
serializer->deserialize(body, received);
int uid = received.user_id();
```

---

## 扩展新序列化器

```cpp
// 实现 Serializer 接口即可
class XmlSerializer : public rpc::Serializer {
public:
    std::string name() const override { return "xml"; }
    rpc::SerializerType type() const override {
        return static_cast<rpc::SerializerType>(2);  // 新增类型
    }

    std::string serialize(const google::protobuf::Message& msg) override {
        // XML 序列化逻辑...
    }

    bool deserialize(const std::string& data,
                     google::protobuf::Message& msg) override {
        // XML 反序列化逻辑...
    }
};
```

---

## 测试结果

| 测试 | 内容 | 结果 |
|------|------|------|
| create by enum | 枚举工厂创建 | OK |
| create by string | 字符串工厂创建 | OK |
| unknown throws | 未知序列化器抛异常 | OK |
| protobuf roundtrip | User 消息往返 | OK |
| protobuf garbage | 损坏数据返回 false | OK |
| json roundtrip | User 消息 JSON 往返 | OK |
| json bad input | 非法 JSON 返回 false | OK |
| different output | 两种序列化器输出不同 | OK |
| type matches name | name/type 一致 | OK |
| nested message | GetUserResponse 嵌套消息 | OK |

---

## 编译依赖

- protobuf (vcpkg `protobuf_x64-mingw-static`)
- abseil (protobuf 的依赖，自动引入)
- CMake: `find_package(protobuf CONFIG REQUIRED)`, `target_link_libraries(... protobuf::libprotobuf)`
