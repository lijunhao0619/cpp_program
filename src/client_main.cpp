#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#endif

#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <vector>
#include <algorithm>
#include <numeric>
#include <sstream>

#include "load_config/load_config.h"
#include "load_config/log_init.h"
#include "core/rpc_client.h"
#include "protocol/rpc_protocol.h"

using namespace std::chrono_literals;

// ── 压测模式：输出 machine-readable 单行结果便于脚本收集 ──
static int run_stress(const std::string& host, uint16_t port, int count) {
    rpc::RpcClient client;

    try {
        auto conn_fut = client.connect(host, port);
        conn_fut.get();
    } catch (const std::exception& e) {
        std::cerr << "STRESS_ERROR: connect failed: " << e.what() << std::endl;
        return 1;
    }

    int success = 0, fail = 0;
    std::vector<double> latencies;
    latencies.reserve(count);

    auto t1 = std::chrono::steady_clock::now();

    for (int i = 0; i < count; i++) {
        auto req_start = std::chrono::steady_clock::now();

        auto fut = client.call("PingService", "Ping", "stress_" + std::to_string(i));
        auto resp = fut.get();

        auto req_end = std::chrono::steady_clock::now();
        auto req_us = std::chrono::duration_cast<std::chrono::microseconds>(
            req_end - req_start).count();

        if (resp.error_code == 0 && resp.result == "pong") {
            success++;
            latencies.push_back(req_us / 1000.0);
        } else {
            fail++;
        }
    }

    auto t2 = std::chrono::steady_clock::now();
    auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();

    client.disconnect();

    // 计算统计
    double avg_ms = 0, min_ms = 0, max_ms = 0;
    if (!latencies.empty()) {
        std::sort(latencies.begin(), latencies.end());
        avg_ms = std::accumulate(latencies.begin(), latencies.end(), 0.0) / latencies.size();
        min_ms = latencies.front();
        max_ms = latencies.back();
    }

    double qps = (total_ms > 0) ? (success * 1000.0 / total_ms) : 0;

    // 机器可读单行输出 (key=value 格式)
    std::cout << "STRESS OK count=" << count
              << " success=" << success
              << " fail=" << fail
              << " total_ms=" << total_ms
              << " qps=" << qps
              << " avg_ms=" << avg_ms
              << " min_ms=" << min_ms
              << " max_ms=" << max_ms
              << std::endl;

    return 0;
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    // ── 解析命令行参数 ──
    std::string config_path = "src/config/server_config.json";
    std::string server_host = "127.0.0.1";
    uint16_t    server_port = 8080;
    bool        host_set = false;
    bool        port_set = false;
    bool        stress_mode = false;
    int         stress_count = 100;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) {
            config_path = argv[++i];
        } else if (arg == "--host" && i + 1 < argc) {
            server_host = argv[++i];
            host_set = true;
        } else if (arg == "--port" && i + 1 < argc) {
            server_port = static_cast<uint16_t>(std::stoi(argv[++i]));
            port_set = true;
        } else if (arg == "--stress") {
            stress_mode = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                stress_count = std::stoi(argv[++i]);
            }
        } else if (arg == "--help") {
            std::cout << "RPC Client\n"
                      << "  --config  <path>   config file path\n"
                      << "  --host    <host>   server host\n"
                      << "  --port    <port>   server port\n"
                      << "  --stress  [N]      stress test mode (N reqs, default 100)\n"
                      << "  --help              show this help\n";
            return 0;
        }
    }

    // ── 压测模式 ──
    if (stress_mode) {
        return run_stress(server_host, server_port, stress_count);
    }

    // ── 正常交互模式 ──

    auto cfg = rpc::load_config(config_path);
    if (host_set) cfg.server.host = server_host;
    if (port_set) cfg.server.port = server_port;

    std::cout << "=== RPC Client ===\n"
              << "Server: " << cfg.server.host << ":" << cfg.server.port
              << std::endl;

    // ── 初始化日志 ──
    rpc::init_logger(cfg.log.dir, "client.log",
                    spdlog::level::from_str(cfg.log.level));

    // ── 连接服务器 ──
    rpc::RpcClient client;
    std::cout << "Connecting to " << cfg.server.host << ":"
              << cfg.server.port << " ..." << std::endl;

    try {
        auto conn_fut = client.connect(cfg.server.host, cfg.server.port);
        conn_fut.get();
        std::cout << "Connected!" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Failed to connect: " << e.what() << std::endl;
        return 1;
    }

    // ── 调用 Ping ──
    std::cout << "\n--- Call PingService::Ping ---" << std::endl;
    {
        auto fut = client.call("PingService", "Ping", "");
        auto resp = fut.get();
        if (resp.error_code == 0) {
            std::cout << "  Result: " << resp.result << std::endl;
        } else {
            std::cout << "  Error: [" << resp.error_code << "] "
                      << resp.error_msg << std::endl;
        }
    }

    // ── 调用 Stats ──
    std::cout << "\n--- Call PingService::Stats ---" << std::endl;
    {
        auto fut = client.call("PingService", "Stats", "");
        auto resp = fut.get();
        if (resp.error_code == 0) {
            std::cout << "  Result: " << resp.result << std::endl;
        } else {
            std::cout << "  Error: [" << resp.error_code << "] "
                      << resp.error_msg << std::endl;
        }
    }

    // ── 调用不存在的服务 ──
    std::cout << "\n--- Call NonExistentService::Foo ---" << std::endl;
    {
        auto fut = client.call("NonExistentService", "Foo", "");
        auto resp = fut.get();
        std::cout << "  Error: [" << resp.error_code << "] "
                  << resp.error_msg << std::endl;
    }

    // ── 性能测试：连续多次调用 ──
    std::cout << "\n--- Performance: 100 Ping calls ---" << std::endl;
    {
        auto t1 = std::chrono::steady_clock::now();

        int success = 0, fail = 0;
        for (int i = 0; i < 100; i++) {
            auto fut = client.call("PingService", "Ping", "msg" + std::to_string(i));
            auto resp = fut.get();
            if (resp.error_code == 0 && resp.result == "pong")
                success++;
            else
                fail++;
        }

        auto t2 = std::chrono::steady_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count();

        std::cout << "  Success: " << success << " / Fail: " << fail << "\n"
                  << "  Time: " << ms << "ms\n"
                  << "  QPS: " << (100 * 1000 / (ms > 0 ? ms : 1)) << std::endl;
    }

    // ── 断开 ──
    client.disconnect();
    std::cout << "\nDisconnected." << std::endl;

    return 0;
}
