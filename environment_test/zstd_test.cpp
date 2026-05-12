// ============================================================
// zstd (Zstandard) 常用操作教程
// 官方文档: https://facebook.github.io/zstd/
// ============================================================

#include <zstd.h>
#include <zdict.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <chrono>
#include <iostream>

int main() {
    printf("=== 1. 基础压缩 & 解压 ===\n");

    // 1a. 准备数据
    std::string original = "Hello zstd! "
        "This is a repeated message for compression testing. "
        "zstd can compress repeated patterns very efficiently. "
        "Hello zstd! Hello zstd! Hello zstd! Compression is great!";
    size_t src_size = original.size();

    // 1b. 分配压缩缓冲区 —— 用 ZSTD_compressBound 获取最坏情况大小
    size_t bound = ZSTD_compressBound(src_size);
    void* compressed = malloc(bound);

    // 1c. 压缩 —— 第 4 个参数是压缩级别 (1~22，默认 3)
    // 级别越高压缩率越高但越慢，1=最快，3=默认平衡，19+=极高压缩
    size_t comp_size = ZSTD_compress(compressed, bound,
                                      original.data(), src_size,
                                      1);  // 级别 1 = 速度优先

    printf("原始大小: %zu bytes\n", src_size);
    printf("压缩大小: %zu bytes (%.1f%%)\n",
           comp_size, 100.0 * comp_size / src_size);

    // 1d. 解压 —— 先获取原始大小
    unsigned long long decomp_size = ZSTD_getFrameContentSize(compressed, comp_size);
    void* decompressed = malloc(decomp_size);
    ZSTD_decompress(decompressed, decomp_size, compressed, comp_size);

    printf("解压后: %s\n\n", (char*)decompressed);

    free(compressed);
    free(decompressed);

    // ╔══════════════════════════════════════════════════════╗
    // ║  2. 压缩级别对比 (1~19)                             ║
    // ╚══════════════════════════════════════════════════════╝

    printf("=== 2. 压缩级别对比 ===\n");
    // 造一段大文本
    std::string big_data;
    for (int i = 0; i < 100; i++) {
        big_data += "RPC message payload with repeated patterns. ";
    }

    for (int level : {1, 3, 6, 12, 19}) {
        size_t buf_size = ZSTD_compressBound(big_data.size());
        void* buf = malloc(buf_size);
        auto t0 = std::chrono::steady_clock::now();
        size_t result = ZSTD_compress(buf, buf_size,
                                       big_data.data(), big_data.size(), level);
        auto t1 = std::chrono::steady_clock::now();
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
        printf("level=%2d  %zu -> %zu bytes (%.1f%%)  %lld us\n",
               level, big_data.size(), result,
               100.0 * result / big_data.size(), us);
        free(buf);
    }
    printf("\n");

    // ╔══════════════════════════════════════════════════════╗
    // ║  3. 流式压缩 —— 适合大数据 / 流式传输              ║
    // ╚══════════════════════════════════════════════════════╝

    printf("=== 3. 流式压缩 ===\n");

    // 3a. 创建压缩流
    ZSTD_CCtx* cctx = ZSTD_createCCtx();
    ZSTD_CCtx_setParameter(cctx, ZSTD_c_compressionLevel, 3);

    // 分配输出缓冲区
    size_t out_buf_size = ZSTD_CStreamOutSize();
    void* out_buf = malloc(out_buf_size);

    // 开始一个压缩帧
    ZSTD_inBuffer input = {big_data.data(), big_data.size(), 0};

    bool last_chunk = false;
    do {
        ZSTD_outBuffer output = {out_buf, out_buf_size, 0};
        if (input.pos < input.size) {
            ZSTD_compressStream2(cctx, &output, &input, ZSTD_e_continue);
        } else {
            ZSTD_compressStream2(cctx, &output, &input, ZSTD_e_end);
            last_chunk = true;
        }
        // output.pos 字节已写入输出
    } while (!last_chunk);

    printf("流式压缩完成 (CStream)\n");

    ZSTD_freeCCtx(cctx);
    free(out_buf);

    // 3b. 流式解压
    printf("=== 4. 流式解压 ===\n");

    // 先压缩一份数据用于解压测试
    size_t d_bound = ZSTD_compressBound(big_data.size());
    void* d_compressed = malloc(d_bound);
    size_t d_comp_size = ZSTD_compress(d_compressed, d_bound,
                                        big_data.data(), big_data.size(), 3);

    ZSTD_DCtx* dctx = ZSTD_createDCtx();
    size_t d_out_size = ZSTD_DStreamOutSize();
    void* d_out_buf = malloc(d_out_size);
    size_t total_decompressed = 0;

    ZSTD_inBuffer d_input = {d_compressed, d_comp_size, 0};
    while (d_input.pos < d_input.size) {
        ZSTD_outBuffer d_output = {d_out_buf, d_out_size, 0};
        ZSTD_decompressStream(dctx, &d_output, &d_input);
        total_decompressed += d_output.pos;  // 累积解压数据
    }

    printf("流式解压完成: %zu bytes\n", total_decompressed);

    ZSTD_freeDCtx(dctx);
    free(d_out_buf);
    free(d_compressed);

    // ╔══════════════════════════════════════════════════════╗
    // ║  5. 使用字典压缩 —— 小数据压缩的神器                ║
    // ╚══════════════════════════════════════════════════════╝
    // 适合：大量重复结构的小数据（如 RPC 序列化后的 protobuf）
    // 先用一批样本训练字典，然后用字典压缩能大幅提升压缩率

    printf("\n=== 5. 字典压缩 ===\n");

    // 模拟 100 条结构相似的 RPC 请求
    std::vector<std::string> samples;
    for (int i = 0; i < 100; i++) {
        samples.push_back(
            R"({"service":")" + std::to_string(i % 4) +
            R"(","method":"Add","params":[)" +
            std::to_string(i) + R"(,") +
            std::to_string(i * 2) + R"(",") +
            std::to_string(i * 3) + R"(]})"
        );
    }

    // 5a. 训练字典
    size_t total_samples_size = 0;
    for (auto& s : samples) total_samples_size += s.size();

    // 拼接所有样本到连续内存
    std::vector<char> samples_buf;
    std::vector<size_t> sample_sizes;
    for (auto& s : samples) {
        samples_buf.insert(samples_buf.end(), s.begin(), s.end());
        sample_sizes.push_back(s.size());
    }

    size_t dict_capacity = 112640;  // zstd 推荐字典大小 (110 KB)
    void* dict_buf = malloc(dict_capacity);
    size_t dict_size = ZDICT_trainFromBuffer(
        dict_buf, dict_capacity,
        samples_buf.data(), sample_sizes.data(),
        (unsigned)sample_sizes.size());
    if (ZDICT_isError(dict_size)) {
        printf("字典训练失败: %s\n", ZDICT_getErrorName(dict_size));
        // 降级：不使用字典
        dict_size = 0;
    } else {
        printf("字典训练完成: %zu bytes\n", dict_size);
    }

    // 5b. 用字典压缩新数据
    if (dict_size > 0) {
        std::string new_request = R"({"service":"Math","method":"Div","params":[10,2]})";

        size_t precomp_size = ZSTD_compressBound(new_request.size());
        void* precomp_buf = malloc(precomp_size);
        ZSTD_CCtx* precomp_ctx = ZSTD_createCCtx();

        ZSTD_CCtx_loadDictionary(precomp_ctx, dict_buf, dict_size);
        ZSTD_CCtx_setParameter(precomp_ctx, ZSTD_c_compressionLevel, 3);

        size_t precomp_result = ZSTD_compress2(precomp_ctx,
            precomp_buf, precomp_size,
            new_request.data(), new_request.size());

        printf("无字典: %zu -> ~%zu bytes\n",
               new_request.size(), ZSTD_compressBound(new_request.size()));
        printf("有字典: %zu -> %zu bytes (%.1f%%)\n",
               new_request.size(), precomp_result,
               100.0 * precomp_result / new_request.size());

        // 5c. 用字典解压
        size_t pre_decomp_size = ZSTD_getFrameContentSize(precomp_buf, precomp_result);
        void* pre_decomp_buf = malloc(pre_decomp_size);
        ZSTD_DCtx* pre_dctx = ZSTD_createDCtx();
        ZSTD_DCtx_loadDictionary(pre_dctx, dict_buf, dict_size);
        ZSTD_decompressDCtx(pre_dctx, pre_decomp_buf, pre_decomp_size,
                             precomp_buf, precomp_result);
        printf("字典解压: %s\n", (char*)pre_decomp_buf);

        ZSTD_freeCCtx(precomp_ctx);
        ZSTD_freeDCtx(pre_dctx);
        free(precomp_buf);
        free(pre_decomp_buf);
    }
    free(dict_buf);

    // ╔══════════════════════════════════════════════════════╗
    // ║  6. 创建压缩上下文复用 —— 高频调用场景              ║
    // ╚══════════════════════════════════════════════════════╝
    // 创建一次的 CCtx/DCtx 可以反复使用，节省内存分配开销

    printf("\n=== 6. 复用压缩上下文 ===\n");

    ZSTD_CCtx* reuse_cctx = ZSTD_createCCtx();
    ZSTD_CCtx_setParameter(reuse_cctx, ZSTD_c_compressionLevel, 1);

    for (int i = 0; i < 5; i++) {
        std::string msg = "Repeated RPC call #" + std::to_string(i);
        size_t b = ZSTD_compressBound(msg.size());
        char* c = (char*)malloc(b);

        // compress2 方法需要 CCtx_setParameter 配合
        // 或者直接用不带上下文的 ZSTD_compress 也可以
        size_t result = ZSTD_compress(c, b, msg.data(), msg.size(), 1);

        printf("msg[%d]: %zu -> %zu bytes\n", i, msg.size(), result);
        free(c);
    }
    ZSTD_freeCCtx(reuse_cctx);

    // ╔══════════════════════════════════════════════════════╗
    // ║  7. 压缩边界检查 & 错误处理                         ║
    // ╚══════════════════════════════════════════════════════╝

    printf("\n=== 7. 错误处理 ===\n");

    const char* bad_data = "some text";
    size_t bad_size = strlen(bad_data);

    // ZSTD_decompress 解码非 zstd 数据会返回错误
    size_t err_result = ZSTD_decompress(nullptr, 0, bad_data, bad_size);
    if (ZSTD_isError(err_result)) {
        printf("解压失败(预期): %s\n", ZSTD_getErrorName(err_result));
    }

    // ZSTD_compressBound 不会失败，只可能溢出（极少见）
    printf("compressBound(0) = %zu\n", ZSTD_compressBound(0));
    printf("compressBound(1MB) = %zu\n", ZSTD_compressBound(1024 * 1024));

    // ╔══════════════════════════════════════════════════════╗
    // ║  8. RPC 场景推荐参数                                ║
    // ╚══════════════════════════════════════════════════════╝

    printf("\n=== 8. RPC 场景推荐 ===\n");
    printf("小消息 (< 1KB):   level=1 (速度优先，压缩开销低)\n");
    printf("中消息 (1KB~1MB): level=3 (默认平衡)\n");
    printf("大消息 (> 1MB):   level=3~6, 使用流式压缩\n");
    printf("高频同类消息:     使用字典压缩，大幅提升压缩率\n");
    printf("持久化/冷数据:    level=19~22 (极致压缩)\n");

    printf("\nzstd OK!\n");
    return 0;
}
