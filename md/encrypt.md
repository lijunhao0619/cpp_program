# 数据加密模块设计文档

## 概述

基于 OpenSSL EVP 的 AES-256-GCM 认证加密模块，为 RPC 通信提供数据机密性和完整性保护。

## 文件位置

| 文件 | 用途 |
|------|------|
| `src/encrypt/encrypt.h` | API 声明 |
| `src/encrypt/encrypt.cpp` | AES-256-GCM 实现 |

---

## 加密方案选型

```
方案对比：
  AES-CBC      → 需额外 HMAC，两遍 pass
  AES-GCM ✅   → 认证加密（AEAD），单遍 pass，内置完整性校验
  ChaCha20     → 无硬件加速时更快，但 OpenSSL EVP 接口一致可替换
```

**选择 AES-256-GCM 的理由：**
- 认证加密：同时提供机密性和数据完整性（防篡改）
- GCM tag 自动验证，解密时检测到篡改直接抛异常
- Intel AES-NI 硬件加速，性能优异
- OpenSSL EVP API 封装，易于使用

---

## 架构设计

### 加密流程

```
generate_key() ──▶ 32 字节随机密钥
generate_iv()  ──▶ 12 字节随机 nonce

aes_encrypt(plaintext, key, iv):
    │
    ├── EVP_CIPHER_CTX_new()
    ├── EVP_EncryptInit_ex(EVP_aes_256_gcm())
    ├── EVP_CIPHER_CTX_ctrl(SET_IVLEN, 12)
    ├── EVP_EncryptInit_ex(key, iv)
    ├── EVP_EncryptUpdate() → 加密数据
    ├── EVP_EncryptFinal_ex() → 结束加密
    ├── EVP_CIPHER_CTX_ctrl(GET_TAG, 16) → 获取认证标签
    └── 返回: ciphertext + tag（tag 在末尾 16 字节）
```

### 解密流程

```
aes_decrypt(ciphertext+tag, key, iv):
    │
    ├── 分离最后 16 字节 → tag
    ├── EVP_CIPHER_CTX_new()
    ├── EVP_DecryptInit_ex(EVP_aes_256_gcm())
    ├── EVP_CIPHER_CTX_ctrl(SET_IVLEN, 12)
    ├── EVP_DecryptInit_ex(key, iv)
    ├── EVP_DecryptUpdate() → 解密数据
    ├── EVP_CIPHER_CTX_ctrl(SET_TAG, 16, tag) → 设置期望的 tag
    ├── EVP_DecryptFinal_ex() → 验证 tag + 结束
    │       ret != 1 → 认证失败（密钥错误或数据被篡改）
    └── 返回: 明文
```

---

## API 设计

```cpp
// 生成随机 256-bit 密钥（32 字节）
std::string generate_key();

// 生成随机 96-bit IV / nonce（12 字节，GCM 推荐长度）
std::string generate_iv();

// AES-256-GCM 加密
// plaintext: 明文
// key:       32 字节密钥
// iv:        12 字节 IV
// 返回: ciphertext + 16 字节 GCM tag（tag 在末尾 16 字节）
std::string aes_encrypt(const std::string& plaintext,
                        const std::string& key,
                        const std::string& iv);

// AES-256-GCM 解密
// ciphertext: 密文 + 16 字节 tag
// key:        32 字节密钥
// iv:         12 字节 IV
// 返回: 明文
// 异常: tag 校验失败抛 std::runtime_error("authentication failed...")
std::string aes_decrypt(const std::string& ciphertext,
                        const std::string& key,
                        const std::string& iv);
```

---

## 使用示例

```cpp
#include "encrypt/encrypt.h"

// 生成密钥和 IV
std::string key = rpc::generate_key();  // 32 bytes
std::string iv  = rpc::generate_iv();   // 12 bytes

// 加密
std::string plaintext = "sensitive data";
std::string ciphertext = rpc::aes_encrypt(plaintext, key, iv);
// ciphertext = encrypted_data + 16-byte tag

// 解密
std::string decrypted = rpc::aes_decrypt(ciphertext, key, iv);
assert(decrypted == plaintext);

// 错误密钥 → 抛异常
try {
    std::string wrong_key = rpc::generate_key();
    rpc::aes_decrypt(ciphertext, wrong_key, iv);
} catch (const std::runtime_error& e) {
    // authentication failed (wrong key or tampered data)
}
```

### RPC 集成

```cpp
// 连接建立时交换密钥（通过 TLS 或预共享密钥）
void on_session_established(std::shared_ptr<Session> sess) {
    sess->key = rpc::generate_key();
    sess->iv  = rpc::generate_iv();
    // 通过安全通道发送 key + iv 给对端...
}

// 发送加密消息
void send_message(Session& sess, const Message& msg) {
    std::string plain = serializer.serialize(msg);
    std::string body = rpc::compress(plain);   // 先压缩
    std::string encrypted = rpc::aes_encrypt(body, sess.key, sess.iv);
    transport.send(encrypted);
}
```

---

## 安全要点

| 事项 | 说明 |
|------|------|
| **IV 唯一性** | 同一密钥下，每次加密必须使用不同 IV，否则破坏 GCM 安全性 |
| **密钥管理** | 密钥通过 TLS 或预共享方式交换；不通过明文传输 |
| **Tag 长度** | 16 字节（128-bit），足够抵抗碰撞攻击 |
| **认证优先** | 解密前先验证 tag，无效数据直接拒绝，防止 padding oracle 等攻击 |
| **随机源** | 使用 `RAND_bytes()`（OpenSSL 的 CSPRNG），不依赖 `rand()` |

---

## 测试结果

| 测试 | 内容 | 结果 |
|------|------|------|
| generate_key 32 bytes | 密钥长度正确 | OK |
| generate_iv 12 bytes | IV 长度正确 | OK |
| generate_key random | 两次生成不相等 | OK |
| generate_iv random | 两次生成不相等 | OK |
| roundtrip small | 小文本加密/解密 | OK |
| roundtrip large | 100KB 数据往返 | OK |
| roundtrip binary | 二进制数据往返 | OK |
| ciphertext length | ct = pt + 16 (tag) | OK |
| wrong key detected | 错误密钥抛异常 | OK |
| wrong IV detected | 错误 IV 抛异常 | OK |
| tampered ct detected | 篡改密文抛异常 | OK |
| tampered tag detected | 篡改 tag 抛异常 | OK |
| invalid key size | 密钥长度错误抛异常 | OK |
| invalid IV size | IV 长度错误抛异常 | OK |
| ct too short | 密文过短抛异常 | OK |
| empty plaintext | 空明文往返正确 | OK |

---

## 编译依赖

- OpenSSL (libssl + libcrypto)，通过 MSYS2 安装
- 头文件: `-I/e/msys/ucrt64/include`（openssl/evp.h, openssl/rand.h）
- 链接: `-lssl -lcrypto`
