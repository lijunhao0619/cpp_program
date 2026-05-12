# 数据压缩模块设计文档

## 概述

基于 zstd（Zstandard）的数据压缩/解压模块，用于 RPC 消息体积压缩，降低网络传输开销。

## 文件位置

| 文件 | 用途 |
|------|------|
| `src/compress_data/compress.h` | API 声明 + `CompressInfo` 结构体 |
| `src/compress_data/compress.cpp` | zstd 压缩/解压实现 |

---

## 架构设计

```
原始数据 ──▶ ZSTD_compress() ──▶ 压缩数据（可通过网络传输）
                                        │
压缩数据 ──▶ ZSTD_decompress() ──▶ 原始数据（校验帧头）
```

### 压缩流程

```
compress(data, level)
    │
    ├── ZSTD_compressBound(data.size())  → 预估压缩后最大大小
    ├── ZSTD_compress(buf, bound, data, size, level)
    │       level: 1-22, 默认 3（平衡速度与压缩率）
    └── resize(actual) → 返回精确大小的压缩数据
```

### 解压流程

```
decompress(data)
    │
    ├── ZSTD_getFrameContentSize() → 从帧头读取原始大小
    │       返回值: 原始大小 / ZSTD_CONTENTSIZE_ERROR / ZSTD_CONTENTSIZE_UNKNOWN
    ├── 预分配原始大小的缓冲区
    ├── ZSTD_decompress(out, out_size, data, size)
    └── resize(actual) → 返回原始数据
```

---

## API 设计

```cpp
// zstd 压缩
// level: 1-22, 默认 3（平衡速度与压缩率）
// 空数据直接返回空字符串
std::string compress(const std::string& data, int level = 3);

// zstd 解压
// 空数据直接返回空字符串
// 数据损坏时抛出 std::runtime_error
std::string decompress(const std::string& data);

// 压缩信息（诊断用）
struct CompressInfo {
    size_t original_size;
    size_t compressed_size;
    double ratio;  // compressed / original
};

CompressInfo compress_info(const std::string& original,
                           const std::string& compressed);
```

---

## 使用示例

```cpp
#include "compress_data/compress.h"

// 压缩
std::string original = get_large_payload();
std::string compressed = rpc::compress(original, 3);

// 诊断
auto info = rpc::compress_info(original, compressed);
std::cout << "压缩率: " << info.ratio * 100 << "%\n";

// 解压
std::string restored = rpc::decompress(compressed);
assert(restored == original);
```

### RPC 集成

```cpp
// 发送前压缩
std::string payload = serializer.serialize(message);
std::string body = rpc::compress(payload);   // 压缩消息体

// 接收后解压
std::string body = rpc::decompress(raw_data);
Message msg = serializer.deserialize(body);
```

---

## 压缩级别参考

| Level | 速度 | 压缩率 | 适用场景 |
|-------|------|--------|----------|
| 1 | 极快 | 较低 | 实时通信、低延迟 |
| 3 (默认) | 快 | 中等 | 通用 RPC 场景 |
| 5-10 | 中等 | 较高 | 带宽敏感 |
| 19-22 | 慢 | 极高 | 离线归档、配置同步 |

---

## 测试结果

| 测试 | 内容 | 结果 |
|------|------|------|
| roundtrip small string | 小字符串压缩/解压 | OK |
| roundtrip large data | 100KB 数据往返 | OK |
| roundtrip binary data | 二进制数据（含\0） | OK |
| empty string | 空字符串处理 | OK (返回空) |
| compress_info ratio | 高重复数据压缩率 < 0.5 | OK |
| compress_info sizes | 尺寸信息正确 | OK |
| decompress invalid | 损坏数据抛异常 | OK |
| level 1 (fast) | 50KB level 1 往返 | OK |
| level 19 (best) | 50KB level 19 往返 | OK |

---

## 编译依赖

- zstd (libzstd)，通过 vcpkg 安装
- CMake: `find_package(zstd CONFIG REQUIRED)`, `target_link_libraries(... zstd::libzstd)`
