#include <iostream>
#include <string>
#include <cassert>

#include "compress_data/compress.h"
#include "encrypt/encrypt.h"

static int passed = 0;
static int failed = 0;

void ok() { std::cout << "OK\n"; passed++; }
void fail(const std::string& msg) { std::cout << "FAIL: " << msg << "\n"; failed++; }

int main() {
    std::cout << "=== Compress Module Tests ===\n";

    // ── compress / decompress roundtrip ──
    std::cout << "  roundtrip small string ... ";
    {
        std::string original = "Hello, RPC framework! This is a test string for zstd compression.";
        std::string c = rpc::compress(original);
        std::string d = rpc::decompress(c);
        if (d == original) ok(); else fail("mismatch");
    }

    std::cout << "  roundtrip large data ... ";
    {
        std::string original(100000, '\0');
        for (size_t i = 0; i < original.size(); i++)
            original[i] = static_cast<char>('A' + (i % 26));
        std::string c = rpc::compress(original);
        std::string d = rpc::decompress(c);
        if (d == original) ok(); else fail("mismatch");
    }

    std::cout << "  roundtrip binary data ... ";
    {
        std::string original(4096, '\0');
        for (size_t i = 0; i < original.size(); i++)
            original[i] = static_cast<char>(i % 256);
        std::string c = rpc::compress(original);
        std::string d = rpc::decompress(c);
        if (d == original) ok(); else fail("mismatch");
    }

    std::cout << "  empty string ... ";
    {
        std::string c = rpc::compress("");
        std::string d = rpc::decompress("");
        if (c.empty() && d.empty()) ok(); else fail("expected empty");
    }

    std::cout << "  compress_info ratio ... ";
    {
        std::string original(10000, 'A');
        std::string c = rpc::compress(original);
        auto info = rpc::compress_info(original, c);
        if (info.ratio < 0.5) ok(); else fail("expected ratio < 0.5 for repetitive data, got " + std::to_string(info.ratio));
    }

    std::cout << "  compress_info sizes ... ";
    {
        std::string original(500, 'B');
        std::string c = rpc::compress(original);
        auto info = rpc::compress_info(original, c);
        if (info.original_size == 500 && info.compressed_size == c.size()) ok(); else fail("size mismatch");
    }

    std::cout << "  decompress invalid data ... ";
    {
        try {
            rpc::decompress("this is not valid zstd data!!!");
            fail("expected exception");
        } catch (const std::runtime_error&) {
            ok();
        }
    }

    std::cout << "  compress level 1 (fast) ... ";
    {
        std::string original(50000, 'A');
        for (size_t i = 0; i < original.size(); i++) original[i] = static_cast<char>('A' + (i % 26));
        std::string c = rpc::compress(original, 1);
        std::string d = rpc::decompress(c);
        if (d == original) ok(); else fail("mismatch");
    }

    std::cout << "  compress level 19 (best) ... ";
    {
        std::string original(50000, 'A');
        for (size_t i = 0; i < original.size(); i++) original[i] = static_cast<char>('A' + (i % 26));
        std::string c = rpc::compress(original, 19);
        std::string d = rpc::decompress(c);
        if (d == original) ok(); else fail("mismatch");
    }

    std::cout << "\n=== Encrypt Module Tests ===\n";

    // ── key / IV generation ──
    std::cout << "  generate_key returns 32 bytes ... ";
    {
        std::string key = rpc::generate_key();
        if (key.size() == 32) ok(); else fail("size = " + std::to_string(key.size()));
    }

    std::cout << "  generate_iv returns 12 bytes ... ";
    {
        std::string iv = rpc::generate_iv();
        if (iv.size() == 12) ok(); else fail("size = " + std::to_string(iv.size()));
    }

    std::cout << "  generate_key is random ... ";
    {
        std::string k1 = rpc::generate_key();
        std::string k2 = rpc::generate_key();
        if (k1 != k2) ok(); else fail("two keys should differ");
    }

    std::cout << "  generate_iv is random ... ";
    {
        std::string i1 = rpc::generate_iv();
        std::string i2 = rpc::generate_iv();
        if (i1 != i2) ok(); else fail("two IVs should differ");
    }

    // ── encrypt / decrypt roundtrip ──
    std::cout << "  encrypt/decrypt roundtrip small ... ";
    {
        std::string key = rpc::generate_key();
        std::string iv  = rpc::generate_iv();
        std::string plain = "Hello, secure RPC!";
        std::string ct = rpc::aes_encrypt(plain, key, iv);
        std::string pt = rpc::aes_decrypt(ct, key, iv);
        if (pt == plain) ok(); else fail("mismatch");
    }

    std::cout << "  encrypt/decrypt roundtrip large ... ";
    {
        std::string key = rpc::generate_key();
        std::string iv  = rpc::generate_iv();
        std::string plain(100000, '\0');
        for (size_t i = 0; i < plain.size(); i++)
            plain[i] = static_cast<char>('A' + (i % 26));
        std::string ct = rpc::aes_encrypt(plain, key, iv);
        std::string pt = rpc::aes_decrypt(ct, key, iv);
        if (pt == plain) ok(); else fail("mismatch");
    }

    std::cout << "  encrypt/decrypt roundtrip binary ... ";
    {
        std::string key = rpc::generate_key();
        std::string iv  = rpc::generate_iv();
        std::string plain(4096, '\0');
        for (size_t i = 0; i < plain.size(); i++)
            plain[i] = static_cast<char>(i % 256);
        std::string ct = rpc::aes_encrypt(plain, key, iv);
        std::string pt = rpc::aes_decrypt(ct, key, iv);
        if (pt == plain) ok(); else fail("mismatch");
    }

    std::cout << "  ciphertext length = plaintext + 16 (tag) ... ";
    {
        std::string key = rpc::generate_key();
        std::string iv  = rpc::generate_iv();
        std::string plain(1024, 'X');
        std::string ct = rpc::aes_encrypt(plain, key, iv);
        if (ct.size() == plain.size() + 16) ok(); else fail("expected " + std::to_string(plain.size() + 16) + ", got " + std::to_string(ct.size()));
    }

    std::cout << "  wrong key detected ... ";
    {
        std::string k1 = rpc::generate_key();
        std::string k2 = rpc::generate_key();
        std::string iv = rpc::generate_iv();
        std::string plain = "secret";
        std::string ct = rpc::aes_encrypt(plain, k1, iv);
        try {
            rpc::aes_decrypt(ct, k2, iv);
            fail("expected exception");
        } catch (const std::runtime_error&) {
            ok();
        }
    }

    std::cout << "  wrong IV detected ... ";
    {
        std::string key = rpc::generate_key();
        std::string iv1 = rpc::generate_iv();
        std::string iv2 = rpc::generate_iv();
        std::string plain = "secret";
        std::string ct = rpc::aes_encrypt(plain, key, iv1);
        try {
            rpc::aes_decrypt(ct, key, iv2);
            fail("expected exception");
        } catch (const std::runtime_error&) {
            ok();
        }
    }

    std::cout << "  tampered ciphertext detected ... ";
    {
        std::string key = rpc::generate_key();
        std::string iv  = rpc::generate_iv();
        std::string plain = "tamper me";
        std::string ct = rpc::aes_encrypt(plain, key, iv);
        ct[0] ^= 0xFF;
        try {
            rpc::aes_decrypt(ct, key, iv);
            fail("expected exception");
        } catch (const std::runtime_error&) {
            ok();
        }
    }

    std::cout << "  tampered tag detected ... ";
    {
        std::string key = rpc::generate_key();
        std::string iv  = rpc::generate_iv();
        std::string plain = "tamper tag";
        std::string ct = rpc::aes_encrypt(plain, key, iv);
        ct.back() ^= 0xFF;
        try {
            rpc::aes_decrypt(ct, key, iv);
            fail("expected exception");
        } catch (const std::runtime_error&) {
            ok();
        }
    }

    std::cout << "  invalid key size ... ";
    {
        std::string bad_key(16, 'A');
        std::string iv(12, 'B');
        try {
            rpc::aes_encrypt("hello", bad_key, iv);
            fail("expected exception");
        } catch (const std::runtime_error&) {
            ok();
        }
    }

    std::cout << "  invalid IV size ... ";
    {
        std::string key(32, 'A');
        std::string bad_iv(16, 'B');
        try {
            rpc::aes_encrypt("hello", key, bad_iv);
            fail("expected exception");
        } catch (const std::runtime_error&) {
            ok();
        }
    }

    std::cout << "  ciphertext too short for decrypt ... ";
    {
        std::string key(32, 'A');
        std::string iv(12, 'B');
        try {
            rpc::aes_decrypt("short", key, iv);
            fail("expected exception");
        } catch (const std::runtime_error&) {
            ok();
        }
    }

    std::cout << "  empty plaintext roundtrip ... ";
    {
        std::string key = rpc::generate_key();
        std::string iv  = rpc::generate_iv();
        std::string ct = rpc::aes_encrypt("", key, iv);
        std::string pt = rpc::aes_decrypt(ct, key, iv);
        if (pt.empty()) ok(); else fail("expected empty");
    }

    // ── summary ──
    std::cout << "\n=========================\n";
    std::cout << "Passed: " << passed << "\n";
    std::cout << "Failed: " << failed << "\n";

    return failed > 0 ? 1 : 0;
}
