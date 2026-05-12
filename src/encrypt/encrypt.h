#pragma once

#include <string>
#include <cstdint>

namespace rpc {

// ── AES-256-GCM 加密 ──

// 生成随机 256-bit 密钥
std::string generate_key();

// 生成随机 96-bit IV（nonce）
std::string generate_iv();

// AES-256-GCM 加密
// plaintext: 明文
// key:       32 字节密钥
// iv:        12 字节 IV
// 返回: ciphertext + 16字节 tag（末16字节）
std::string aes_encrypt(const std::string& plaintext,
                        const std::string& key,
                        const std::string& iv);

// AES-256-GCM 解密
// ciphertext: 密文 + 16字节 tag
// key:        32 字节密钥
// iv:         12 字节 IV
// 返回: 明文（tag 校验失败时抛异常）
std::string aes_decrypt(const std::string& ciphertext,
                        const std::string& key,
                        const std::string& iv);

} // namespace rpc
