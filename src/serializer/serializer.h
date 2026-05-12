#pragma once

#include <string>
#include <memory>
#include <stdexcept>

#include <google/protobuf/message.h>
#include <google/protobuf/util/json_util.h>

namespace rpc {

// ── 序列化器类型 ──
enum class SerializerType : uint8_t {
    PROTOBUF = 0,
    JSON     = 1,
};

// ── 抽象序列化器接口 ──
class Serializer {
public:
    virtual ~Serializer() = default;

    // 序列化器名称
    virtual std::string name() const = 0;

    // 序列化类型枚举
    virtual SerializerType type() const = 0;

    // 序列化 protobuf Message → 字节流（用于网络传输）
    virtual std::string serialize(const google::protobuf::Message& msg) = 0;

    // 反序列化 字节流 → protobuf Message
    // 成功返回 true，失败返回 false
    virtual bool deserialize(const std::string& data,
                             google::protobuf::Message& msg) = 0;
};

// ── Protobuf 二进制序列化器 ──
class ProtobufSerializer : public Serializer {
public:
    std::string name() const override { return "protobuf"; }
    SerializerType type() const override { return SerializerType::PROTOBUF; }

    std::string serialize(const google::protobuf::Message& msg) override {
        std::string data;
        if (!msg.SerializeToString(&data)) {
            throw std::runtime_error("protobuf: SerializeToString failed");
        }
        return data;
    }

    bool deserialize(const std::string& data,
                     google::protobuf::Message& msg) override {
        return msg.ParseFromString(data);
    }
};

// ── JSON 序列化器（基于 protobuf util/json_util） ──
class JsonSerializer : public Serializer {
public:
    std::string name() const override { return "json"; }
    SerializerType type() const override { return SerializerType::JSON; }

    std::string serialize(const google::protobuf::Message& msg) override {
        std::string json;
        auto status = google::protobuf::util::MessageToJsonString(msg, &json);
        if (!status.ok()) {
            throw std::runtime_error(
                "json serialize: " + std::string(status.message()));
        }
        return json;
    }

    bool deserialize(const std::string& data,
                     google::protobuf::Message& msg) override {
        auto status = google::protobuf::util::JsonStringToMessage(data, &msg);
        return status.ok();
    }
};

// ── 工厂函数 ──
inline std::unique_ptr<Serializer> create_serializer(SerializerType type) {
    switch (type) {
    case SerializerType::PROTOBUF:
        return std::make_unique<ProtobufSerializer>();
    case SerializerType::JSON:
        return std::make_unique<JsonSerializer>();
    default:
        throw std::runtime_error("unknown serializer type");
    }
}

inline std::unique_ptr<Serializer> create_serializer(const std::string& name) {
    if (name == "protobuf" || name == "proto")
        return std::make_unique<ProtobufSerializer>();
    if (name == "json")
        return std::make_unique<JsonSerializer>();
    throw std::runtime_error("unknown serializer: " + name);
}

} // namespace rpc
