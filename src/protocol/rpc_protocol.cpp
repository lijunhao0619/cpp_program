#include "rpc_protocol.h"

#include <cstring>
#include <stdexcept>

namespace rpc {

// ── 大端序转换 ──
namespace {

inline void write_u32(char* buf, uint32_t val) {
    buf[0] = static_cast<char>((val >> 24) & 0xFF);
    buf[1] = static_cast<char>((val >> 16) & 0xFF);
    buf[2] = static_cast<char>((val >> 8) & 0xFF);
    buf[3] = static_cast<char>(val & 0xFF);
}

inline uint32_t read_u32(const char* buf) {
    return (static_cast<uint32_t>(static_cast<uint8_t>(buf[0])) << 24) |
           (static_cast<uint32_t>(static_cast<uint8_t>(buf[1])) << 16) |
           (static_cast<uint32_t>(static_cast<uint8_t>(buf[2])) << 8)  |
           (static_cast<uint32_t>(static_cast<uint8_t>(buf[3])));
}

inline void write_u16(char* buf, uint16_t val) {
    buf[0] = static_cast<char>((val >> 8) & 0xFF);
    buf[1] = static_cast<char>(val & 0xFF);
}

inline uint16_t read_u16(const char* buf) {
    return (static_cast<uint16_t>(static_cast<uint8_t>(buf[0])) << 8) |
           static_cast<uint16_t>(static_cast<uint8_t>(buf[1]));
}

} // anonymous

// ── 帧头序列化 ──
std::string serialize_header(const RpcHeader& header) {
    std::string data(20, '\0');
    write_u32(&data[0],  header.magic);
    data[4]  = static_cast<char>(header.version);
    data[5]  = static_cast<char>(header.msg_type);
    data[6]  = static_cast<char>(header.serializer);
    data[7]  = static_cast<char>(header.flags);
    write_u32(&data[8],  header.seq_id);
    write_u32(&data[12], header.body_size);
    write_u32(&data[16], header.checksum);
    return data;
}

RpcHeader deserialize_header(const std::string& data) {
    if (data.size() < 20)
        throw std::runtime_error("protocol: header too short");
    RpcHeader h;
    h.magic      = read_u32(&data[0]);
    h.version    = static_cast<uint8_t>(data[4]);
    h.msg_type   = static_cast<uint8_t>(data[5]);
    h.serializer = static_cast<uint8_t>(data[6]);
    h.flags      = static_cast<uint8_t>(data[7]);
    h.seq_id     = read_u32(&data[8]);
    h.body_size  = read_u32(&data[12]);
    h.checksum   = read_u32(&data[16]);
    return h;
}

// ── RpcRequest 序列化 ──
// Wire format:
//   uint16_t service_name_len + service_name
//   uint16_t method_name_len  + method_name
//   uint32_t timeout_ms
//   uint32_t params_len       + params
std::string RpcRequest::serialize() const {
    size_t total = 2 + service_name.size()
                 + 2 + method_name.size()
                 + 4
                 + 4 + params.size();
    std::string data(total, '\0');
    size_t pos = 0;

    write_u16(&data[pos], static_cast<uint16_t>(service_name.size()));
    pos += 2;
    std::memcpy(&data[pos], service_name.data(), service_name.size());
    pos += service_name.size();

    write_u16(&data[pos], static_cast<uint16_t>(method_name.size()));
    pos += 2;
    std::memcpy(&data[pos], method_name.data(), method_name.size());
    pos += method_name.size();

    write_u32(&data[pos], timeout_ms);
    pos += 4;

    write_u32(&data[pos], static_cast<uint32_t>(params.size()));
    pos += 4;
    std::memcpy(&data[pos], params.data(), params.size());

    return data;
}

RpcRequest RpcRequest::deserialize(const std::string& data) {
    if (data.size() < 4) throw std::runtime_error("protocol: invalid request");

    RpcRequest req;
    size_t pos = 0;

    uint16_t srv_len = read_u16(&data[pos]);
    pos += 2;
    if (pos + srv_len > data.size())
        throw std::runtime_error("protocol: request truncated (service_name)");
    req.service_name.assign(&data[pos], srv_len);
    pos += srv_len;

    if (pos + 2 > data.size())
        throw std::runtime_error("protocol: request truncated (method_name)");
    uint16_t method_len = read_u16(&data[pos]);
    pos += 2;
    if (pos + method_len > data.size())
        throw std::runtime_error("protocol: request truncated (method_name)");
    req.method_name.assign(&data[pos], method_len);
    pos += method_len;

    if (pos + 4 > data.size())
        throw std::runtime_error("protocol: request truncated (timeout)");
    req.timeout_ms = read_u32(&data[pos]);
    pos += 4;

    if (pos + 4 > data.size())
        throw std::runtime_error("protocol: request truncated (params_len)");
    uint32_t params_len = read_u32(&data[pos]);
    pos += 4;
    if (pos + params_len > data.size())
        throw std::runtime_error("protocol: request truncated (params)");
    req.params.assign(&data[pos], params_len);

    return req;
}

// ── RpcResponse 序列化 ──
// Wire format:
//   int32_t  error_code
//   uint16_t error_msg_len + error_msg
//   uint32_t result_len    + result
std::string RpcResponse::serialize() const {
    size_t total = 4
                 + 2 + error_msg.size()
                 + 4 + result.size();
    std::string data(total, '\0');
    size_t pos = 0;

    write_u32(&data[pos], static_cast<uint32_t>(error_code));
    pos += 4;

    write_u16(&data[pos], static_cast<uint16_t>(error_msg.size()));
    pos += 2;
    std::memcpy(&data[pos], error_msg.data(), error_msg.size());
    pos += error_msg.size();

    write_u32(&data[pos], static_cast<uint32_t>(result.size()));
    pos += 4;
    std::memcpy(&data[pos], result.data(), result.size());

    return data;
}

RpcResponse RpcResponse::deserialize(const std::string& data) {
    if (data.size() < 6) throw std::runtime_error("protocol: invalid response");

    RpcResponse resp;
    size_t pos = 0;

    resp.error_code = static_cast<int32_t>(read_u32(&data[pos]));
    pos += 4;

    uint16_t errmsg_len = read_u16(&data[pos]);
    pos += 2;
    if (pos + errmsg_len > data.size())
        throw std::runtime_error("protocol: response truncated (error_msg)");
    resp.error_msg.assign(&data[pos], errmsg_len);
    pos += errmsg_len;

    if (pos + 4 > data.size())
        throw std::runtime_error("protocol: response truncated (result_len)");
    uint32_t result_len = read_u32(&data[pos]);
    pos += 4;
    if (pos + result_len > data.size())
        throw std::runtime_error("protocol: response truncated (result)");
    resp.result.assign(&data[pos], result_len);

    return resp;
}

// ── CRC32 校验 ──
// ISO-HDLC CRC32: 多项式 0xEDB88320
uint32_t crc32_checksum(const std::string& data) {
    static uint32_t table[256] = {};
    static bool initialized = false;
    if (!initialized) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t crc = i;
            for (int j = 0; j < 8; j++) {
                crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320UL : 0);
            }
            table[i] = crc;
        }
        initialized = true;
    }

    uint32_t crc = 0xFFFFFFFFUL;
    for (size_t i = 0; i < data.size(); i++) {
        crc = table[(crc ^ static_cast<uint8_t>(data[i])) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFUL;
}

} // namespace rpc
