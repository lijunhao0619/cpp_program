#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <cstring>

namespace rpc {

// ── RPC 消息类型 ──
enum class MsgType : uint8_t {
    REQUEST   = 0x01,  // 客户端 → 服务端：RPC 调用请求
    RESPONSE  = 0x02,  // 服务端 → 客户端：RPC 调用响应
    RPC_ERROR = 0x03,  // 服务端 → 客户端：RPC 调用错误
    HEARTBEAT = 0x04,  // 心跳包
};

// ── 序列化器标识 ──
enum class SerType : uint8_t {
    PROTOBUF = 0,
    JSON     = 1,
};

// ── RPC 协议帧头（20 字节，固定大小） ──
// 网络字节序：大端
struct RpcHeader {
    static constexpr uint32_t MAGIC = 0x52504301;  // "RPC\x01"
    static constexpr uint8_t  VERSION = 1;

    uint32_t magic;       // 魔术字 0x52504301
    uint8_t  version;     // 协议版本号
    uint8_t  msg_type;    // MsgType 枚举
    uint8_t  serializer;  // SerType 枚举（序列化方式）
    uint8_t  flags;       // 标志位：bit0=压缩, bit1=加密
    uint32_t seq_id;      // 请求序列号（用于匹配 request↔response）
    uint32_t body_size;   // Body 长度（字节）
    uint32_t checksum;    // Body 的 CRC32 校验值（0 表示无校验）
};

// 标志位定义
enum RpcFlags : uint8_t {
    FLAG_NONE      = 0x00,
    FLAG_COMPRESS  = 0x01,  // Body 已 zstd 压缩
    FLAG_ENCRYPT   = 0x02,  // Body 已 AES-GCM 加密
};

// ── RPC 请求体 ──
struct RpcRequest {
    std::string service_name;  // 服务名称，如 "UserService"
    std::string method_name;   // 方法名称，如 "GetUser"
    uint32_t    timeout_ms;    // 超时时间（毫秒）
    std::string params;        // 序列化后的参数（protobuf binary 或 JSON string）

    // 序列化为字节流（用于放入帧 body）
    std::string serialize() const;

    // 从字节流反序列化
    static RpcRequest deserialize(const std::string& data);
};

// ── RPC 响应体 ──
struct RpcResponse {
    int32_t     error_code;  // 0 = 成功，非 0 = 错误
    std::string error_msg;   // 错误描述（成功时为空）
    std::string result;      // 序列化后的返回值

    // 序列化为字节流
    std::string serialize() const;

    // 从字节流反序列化
    static RpcResponse deserialize(const std::string& data);
};

// ── 帧头工具函数 ──

// 序列化帧头为网络字节序（大端）
std::string serialize_header(const RpcHeader& header);

// 从网络字节序反序列化帧头
RpcHeader deserialize_header(const std::string& data);

// 简易 CRC32（用于校验）
uint32_t crc32_checksum(const std::string& data);

} // namespace rpc
