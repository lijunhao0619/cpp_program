#pragma once

#include <cstdint>
#include <string>

namespace rpc {

// ── RPC 错误码定义 ──
// 负数: 框架层错误
// 正数: 留给业务自定义
enum class ErrorCode : int32_t {
    // 成功
    OK = 0,

    // 框架错误 (-1 ~ -99)
    SERVICE_NOT_FOUND    = -1,
    METHOD_NOT_FOUND     = -2,
    TIMEOUT              = -3,
    SERIALIZE_ERROR      = -4,
    DESERIALIZE_ERROR    = -5,
    COMPRESS_ERROR       = -6,
    DECOMPRESS_ERROR     = -7,
    ENCRYPT_ERROR        = -8,
    DECRYPT_ERROR        = -9,
    CONNECTION_CLOSED    = -10,
    CONNECTION_REFUSED   = -11,
    INTERNAL_ERROR       = -99,
};

// 错误码转字符串描述
inline std::string error_code_str(ErrorCode ec) {
    switch (ec) {
    case ErrorCode::OK:                 return "OK";
    case ErrorCode::SERVICE_NOT_FOUND:  return "service not found";
    case ErrorCode::METHOD_NOT_FOUND:   return "method not found";
    case ErrorCode::TIMEOUT:            return "timeout";
    case ErrorCode::SERIALIZE_ERROR:    return "serialize error";
    case ErrorCode::DESERIALIZE_ERROR:  return "deserialize error";
    case ErrorCode::COMPRESS_ERROR:     return "compress error";
    case ErrorCode::DECOMPRESS_ERROR:   return "decompress error";
    case ErrorCode::ENCRYPT_ERROR:      return "encrypt error";
    case ErrorCode::DECRYPT_ERROR:      return "decrypt error";
    case ErrorCode::CONNECTION_CLOSED:  return "connection closed";
    case ErrorCode::CONNECTION_REFUSED: return "connection refused";
    case ErrorCode::INTERNAL_ERROR:     return "internal error";
    default:                            return "unknown error";
    }
}

} // namespace rpc
