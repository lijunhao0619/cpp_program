#include "encrypt.h"

#include <openssl/evp.h>
#include <openssl/rand.h>

#include <cstring>
#include <stdexcept>

namespace rpc {

// GCM tag 长度
static constexpr int GCM_TAG_LEN = 16;
static constexpr int KEY_LEN = 32;  // AES-256
static constexpr int IV_LEN  = 12;  // GCM 推荐

std::string generate_key() {
    std::string key(KEY_LEN, '\0');
    if (RAND_bytes(reinterpret_cast<unsigned char*>(key.data()), KEY_LEN) != 1) {
        throw std::runtime_error("aes: failed to generate key");
    }
    return key;
}

std::string generate_iv() {
    std::string iv(IV_LEN, '\0');
    if (RAND_bytes(reinterpret_cast<unsigned char*>(iv.data()), IV_LEN) != 1) {
        throw std::runtime_error("aes: failed to generate IV");
    }
    return iv;
}

std::string aes_encrypt(const std::string& plaintext,
                         const std::string& key,
                         const std::string& iv) {
    if (key.size() != KEY_LEN)
        throw std::runtime_error("aes_encrypt: key must be 32 bytes");
    if (iv.size() != IV_LEN)
        throw std::runtime_error("aes_encrypt: iv must be 12 bytes");

    auto* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) throw std::runtime_error("aes_encrypt: EVP_CIPHER_CTX_new failed");

    // 初始化加密
    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("aes_encrypt: init failed");
    }

    // 设置 IV
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, IV_LEN, nullptr) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("aes_encrypt: set IV len failed");
    }

    if (EVP_EncryptInit_ex(ctx, nullptr, nullptr,
        reinterpret_cast<const unsigned char*>(key.data()),
        reinterpret_cast<const unsigned char*>(iv.data())) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("aes_encrypt: key/IV set failed");
    }

    // 加密
    std::string ciphertext(plaintext.size() + 16, '\0');
    int out_len = 0;

    if (EVP_EncryptUpdate(ctx,
        reinterpret_cast<unsigned char*>(ciphertext.data()), &out_len,
        reinterpret_cast<const unsigned char*>(plaintext.data()),
        static_cast<int>(plaintext.size())) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("aes_encrypt: update failed");
    }

    int total = out_len;

    // 结束加密
    if (EVP_EncryptFinal_ex(ctx,
        reinterpret_cast<unsigned char*>(ciphertext.data()) + total,
        &out_len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("aes_encrypt: final failed");
    }
    total += out_len;
    ciphertext.resize(total);

    // 获取 tag（追加到密文末尾）
    std::string tag(GCM_TAG_LEN, '\0');
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, GCM_TAG_LEN,
        tag.data()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("aes_encrypt: get tag failed");
    }

    EVP_CIPHER_CTX_free(ctx);

    ciphertext += tag;
    return ciphertext;
}

std::string aes_decrypt(const std::string& ciphertext,
                         const std::string& key,
                         const std::string& iv) {
    if (key.size() != KEY_LEN)
        throw std::runtime_error("aes_decrypt: key must be 32 bytes");
    if (iv.size() != IV_LEN)
        throw std::runtime_error("aes_decrypt: iv must be 12 bytes");
    if (ciphertext.size() < static_cast<size_t>(GCM_TAG_LEN))
        throw std::runtime_error("aes_decrypt: ciphertext too short");

    // 分离 tag（最后 16 字节）
    size_t ct_len = ciphertext.size() - GCM_TAG_LEN;
    std::string tag = ciphertext.substr(ct_len);

    auto* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) throw std::runtime_error("aes_decrypt: EVP_CIPHER_CTX_new failed");

    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("aes_decrypt: init failed");
    }

    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, IV_LEN, nullptr) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("aes_decrypt: set IV len failed");
    }

    if (EVP_DecryptInit_ex(ctx, nullptr, nullptr,
        reinterpret_cast<const unsigned char*>(key.data()),
        reinterpret_cast<const unsigned char*>(iv.data())) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("aes_decrypt: key/IV set failed");
    }

    // 解密
    std::string plaintext(ct_len + 16, '\0');
    int out_len = 0;

    if (EVP_DecryptUpdate(ctx,
        reinterpret_cast<unsigned char*>(plaintext.data()), &out_len,
        reinterpret_cast<const unsigned char*>(ciphertext.data()),
        static_cast<int>(ct_len)) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("aes_decrypt: update failed");
    }

    int total = out_len;

    // 设置期望的 tag
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, GCM_TAG_LEN,
        const_cast<char*>(tag.data())) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("aes_decrypt: set tag failed");
    }

    // 验证 tag
    int ret = EVP_DecryptFinal_ex(ctx,
        reinterpret_cast<unsigned char*>(plaintext.data()) + total,
        &out_len);

    EVP_CIPHER_CTX_free(ctx);

    if (ret != 1) {
        throw std::runtime_error("aes_decrypt: authentication failed (wrong key or tampered data)");
    }

    total += out_len;
    plaintext.resize(total);
    return plaintext;
}

} // namespace rpc
