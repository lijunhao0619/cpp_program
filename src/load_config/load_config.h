#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <cstdint>

namespace rpc {

// ── 服务器基础配置 ──
struct ServerConfig {
    std::string host = "0.0.0.0";
    uint16_t    port = 8080;
    int         thread_pool_size = 8;
    int         max_connections  = 10000;
    int         socket_timeout_ms = 5000;
};

// ── ZooKeeper 配置 ──
struct ZkConfig {
    std::string host = "127.0.0.1";
    uint16_t    port = 2181;
    int         session_timeout_ms = 4000;
    std::string root_path = "/rpc";
    std::string services_path = "/rpc/services";
};

// ── 日志配置 ──
struct LogConfig {
    std::string dir     = "logs";
    std::string file    = "rpc.log";
    std::string level   = "info";          // trace/debug/info/warn/error/critical
    int         max_file_size_mb = 10;
    int         max_files = 5;
};

// ── 序列化配置 ──
struct SerializerConfig {
    std::string type = "protobuf";          // protobuf / json
};

// ── 压缩配置 ──
struct CompressConfig {
    bool enable    = true;
    int  threshold = 256;                  // 字节数：小于此值不压缩
    int  level     = 3;                    // zstd 压缩级别 1-22
};

// ── 加密配置 ──
struct EncryptConfig {
    bool        enable = false;
    std::string public_key_path;
    std::string private_key_path;
};

// ── 负载均衡配置 ──
struct BalancerConfig {
    std::string strategy = "round_robin";   // round_robin / random / weighted
};

// ── 汇总配置 ──
struct Config {
    ServerConfig     server;
    ZkConfig         zk;
    LogConfig        log;
    SerializerConfig serializer;
    CompressConfig   compress;
    EncryptConfig    encrypt;
    BalancerConfig   balancer;
};

// 从 JSON 文件加载配置；文件不存在时返回默认配置
Config load_config(const std::string& filepath);

// 打印配置摘要（用于启动诊断）
void print_config(const Config& cfg);

} // namespace rpc
