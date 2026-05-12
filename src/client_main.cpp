#include <iostream>
#include <string>
#include <thread>
#include <chrono>

#include "load_config/load_config.h"
#include "load_config/log_init.h"
#include "core/rpc_client.h"
#include "protocol/rpc_protocol.h"

using namespace std::chrono_literals;

int main(int argc, char* argv[]) {
    // ── 解析命令行参数 ──
    std::string config_path = "src/config/server_config.json";
    std::string server_host = "127.0.0.1";
    uint16_t    server_port = 8080;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) {
            config_path = argv[++i];
        } else if (arg == "--host" && i + 1 < argc) {
            server_host = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            server_port = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if (arg == "--help") {
            std::cout << "RPC Client\n"
                      << "  --config <path>   config file path\n"
                      << "  --host   <host>   server host\n"
                      << "  --port   <port>   server port\n"
                      << "  --help             show this help\n";
            return 0;
        }
    }

    // ── 加载配置 ──
    auto cfg = rpc::load_config(config_path);

    // 使用命令行覆盖
    if (server_host != "127.0.0.1") cfg.server.host = server_host;
    if (server_port != 8080) cfg.server.port = server_port;

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
