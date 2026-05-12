#pragma once

#include <string>
#include <cstdint>

namespace rpc {

// zstd 压缩
// level: 1-22, 默认 3（平衡速度与压缩率）
std::string compress(const std::string& data, int level = 3);

// zstd 解压
std::string decompress(const std::string& data);

// 获取压缩前/后大小信息（用于诊断）
struct CompressInfo {
    size_t original_size;
    size_t compressed_size;
    double ratio; // compressed / original
};

CompressInfo compress_info(const std::string& original,
                           const std::string& compressed);

} // namespace rpc
