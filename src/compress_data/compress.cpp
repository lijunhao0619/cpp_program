#include "compress.h"

#include <zstd.h>
#include <stdexcept>
#include <cstring>

namespace rpc {

std::string compress(const std::string& data, int level) {
    if (data.empty()) return {};

    size_t bound = ZSTD_compressBound(data.size());
    std::string compressed(bound, '\0');

    size_t actual = ZSTD_compress(
        compressed.data(), compressed.size(),
        data.data(), data.size(), level);

    if (ZSTD_isError(actual)) {
        throw std::runtime_error(
            std::string("zstd compress: ") + ZSTD_getErrorName(actual));
    }

    compressed.resize(actual);
    return compressed;
}

std::string decompress(const std::string& data) {
    if (data.empty()) return {};

    // 从压缩帧头获取原始大小
    unsigned long long original_size =
        ZSTD_getFrameContentSize(data.data(), data.size());

    if (original_size == ZSTD_CONTENTSIZE_ERROR) {
        throw std::runtime_error("zstd decompress: invalid frame");
    }
    if (original_size == ZSTD_CONTENTSIZE_UNKNOWN) {
        throw std::runtime_error("zstd decompress: unknown content size");
    }

    std::string out(static_cast<size_t>(original_size), '\0');

    size_t actual = ZSTD_decompress(
        out.data(), out.size(),
        data.data(), data.size());

    if (ZSTD_isError(actual)) {
        throw std::runtime_error(
            std::string("zstd decompress: ") + ZSTD_getErrorName(actual));
    }

    out.resize(actual);
    return out;
}

CompressInfo compress_info(const std::string& original,
                           const std::string& compressed) {
    CompressInfo info;
    info.original_size   = original.size();
    info.compressed_size = compressed.size();
    info.ratio = (info.original_size > 0)
        ? static_cast<double>(info.compressed_size) / info.original_size
        : 0.0;
    return info;
}

} // namespace rpc
