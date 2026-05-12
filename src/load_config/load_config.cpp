#include "load_config.h"

#include <fstream>
#include <iostream>
#include <iomanip>
#include <filesystem>

namespace rpc {

using json = nlohmann::json;

namespace {

ServerConfig parse_server(const json& j) {
    ServerConfig c;
    c.host               = j.value("host", c.host);
    c.port               = j.value("port", c.port);
    c.thread_pool_size   = j.value("thread_pool_size", c.thread_pool_size);
    c.max_connections    = j.value("max_connections", c.max_connections);
    c.socket_timeout_ms  = j.value("socket_timeout_ms", c.socket_timeout_ms);
    return c;
}

ZkConfig parse_zk(const json& j) {
    ZkConfig c;
    c.host               = j.value("host", c.host);
    c.port               = j.value("port", c.port);
    c.session_timeout_ms = j.value("session_timeout_ms", c.session_timeout_ms);
    c.root_path          = j.value("root_path", c.root_path);
    c.services_path      = j.value("services_path", c.services_path);
    return c;
}

LogConfig parse_log(const json& j) {
    LogConfig c;
    c.dir       = j.value("dir", c.dir);
    c.file      = j.value("file", c.file);
    c.level     = j.value("level", c.level);
    c.max_file_size_mb = j.value("max_file_size_mb", c.max_file_size_mb);
    c.max_files = j.value("max_files", c.max_files);
    return c;
}

SerializerConfig parse_serializer(const json& j) {
    SerializerConfig c;
    c.type = j.value("type", c.type);
    return c;
}

CompressConfig parse_compress(const json& j) {
    CompressConfig c;
    c.enable    = j.value("enable", c.enable);
    c.threshold = j.value("threshold", c.threshold);
    c.level     = j.value("level", c.level);
    return c;
}

EncryptConfig parse_encrypt(const json& j) {
    EncryptConfig c;
    c.enable           = j.value("enable", c.enable);
    c.public_key_path  = j.value("public_key_path", c.public_key_path);
    c.private_key_path = j.value("private_key_path", c.private_key_path);
    return c;
}

BalancerConfig parse_balancer(const json& j) {
    BalancerConfig c;
    c.strategy = j.value("strategy", c.strategy);
    return c;
}

} // anonymous namespace
//核心函数
Config load_config(const std::string& filepath) {
    Config cfg;

    // 尝试读取 JSON 文件
    if (std::filesystem::exists(filepath)) {
        try {
            std::ifstream in(filepath);
            json j = json::parse(in);

            if (j.contains("server"))     cfg.server     = parse_server(j["server"]);
            if (j.contains("zk"))         cfg.zk         = parse_zk(j["zk"]);
            if (j.contains("log"))        cfg.log        = parse_log(j["log"]);
            if (j.contains("serializer")) cfg.serializer = parse_serializer(j["serializer"]);
            if (j.contains("compress"))   cfg.compress   = parse_compress(j["compress"]);
            if (j.contains("encrypt"))    cfg.encrypt    = parse_encrypt(j["encrypt"]);
            if (j.contains("balancer"))   cfg.balancer   = parse_balancer(j["balancer"]);

        } catch (const json::parse_error& e) {
            std::cerr << "[config] JSON parse error: " << e.what()
                      << " — using defaults" << std::endl;
        }
    }
    return cfg;
}

void print_config(const Config& cfg) {
    auto& os = std::cout;
    auto sep = [&] { os << "\n──────────────────────────────────────────\n"; };
    auto hdr = [&](const char* title) { os << "  " << title; };
    auto kv  = [&](const char* key, const auto& val) {
        os << "    " << std::left << std::setw(24) << key << val << '\n';
    };

    os << "┌─────────────────────────────────────────┐\n";
    os << "│  RPC Server Configuration               │\n";
    os << "├─────────────────────────────────────────┤\n";

    hdr("▶ Server"); os << '\n';
    kv("host",                cfg.server.host);
    kv("port",                cfg.server.port);
    kv("thread_pool_size",    cfg.server.thread_pool_size);
    kv("max_connections",     cfg.server.max_connections);
    kv("socket_timeout_ms",   cfg.server.socket_timeout_ms);

    sep(); hdr("▶ ZooKeeper"); os << '\n';
    kv("host",                cfg.zk.host);
    kv("port",                cfg.zk.port);
    kv("session_timeout_ms",  cfg.zk.session_timeout_ms);
    kv("root_path",           cfg.zk.root_path);
    kv("services_path",       cfg.zk.services_path);

    sep(); hdr("▶ Log"); os << '\n';
    kv("dir",                 cfg.log.dir);
    kv("file",                cfg.log.file);
    kv("level",               cfg.log.level);
    kv("max_file_size_mb",    cfg.log.max_file_size_mb);
    kv("max_files",           cfg.log.max_files);

    sep(); hdr("▶ Serializer"); os << '\n';
    kv("type",                cfg.serializer.type);

    sep(); hdr("▶ Compress"); os << '\n';
    kv("enable",              (cfg.compress.enable ? "true" : "false"));
    kv("threshold",           cfg.compress.threshold);
    kv("level",               cfg.compress.level);

    sep(); hdr("▶ Encrypt"); os << '\n';
    kv("enable",              (cfg.encrypt.enable ? "true" : "false"));
    kv("public_key_path",     cfg.encrypt.public_key_path.empty()
                                    ? "(empty)" : cfg.encrypt.public_key_path);
    kv("private_key_path",    cfg.encrypt.private_key_path.empty()
                                    ? "(empty)" : cfg.encrypt.private_key_path);

    sep(); hdr("▶ Balancer"); os << '\n';
    kv("strategy",            cfg.balancer.strategy);

    os << "└─────────────────────────────────────────┘\n";
}

} // namespace rpc
