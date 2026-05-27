#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#endif

#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <thread>
#include <chrono>
#include <csignal>
#include <atomic>
#include <functional>

#include "load_config/load_config.h"
#include "load_config/log_init.h"
#include "core/rpc_service.h"
#include "service/service_manager.h"
#include "thread_pool/thread_pool.h"
#include "thread_pool/thread_pool_singleton.h"

// ── 内置 Ping 服务 ──
class PingService : public rpc::Service {
public:
    std::string name() const override { return "PingService"; }
    std::vector<std::string> methods() const override { return {"Ping", "Stats"}; }

    std::string handle(const std::string& method,
                       const std::string& /*params*/) override {
        if (method == "Ping") {
            return "pong";
        }
        if (method == "Stats") {
            return R"({"status":"running","connections":)" +
                   std::to_string(get_conn_count_()) + "}";
        }
        throw std::runtime_error("unknown method: " + method);
    }

    void set_conn_counter(std::function<size_t()> counter_fn) {
        get_conn_count_ = std::move(counter_fn);
    }

private:
    std::function<size_t()> get_conn_count_ = [] { return 0; };
};

namespace {
std::atomic<bool> g_running{true};
}

void signal_handler(int) {
    g_running = false;
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    // ── 解析命令行参数 ──
    std::string config_path = "src/config/server_config.json";
    uint16_t override_port = 0;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) {
            config_path = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            override_port = static_cast<uint16_t>(std::stoi(argv[++i]));
        } else if (arg == "--help") {
            std::cout << "RPC Server\n"
                      << "  --config <path>   config file path\n"
                      << "  --port   <port>   override listen port\n"
                      << "  --help             show this help\n";
            return 0;
        }
    }

    // ── 加载配置 ──
    std::cout << "Loading config: " << config_path << std::endl;
    auto cfg = rpc::load_config(config_path);
    rpc::print_config(cfg);

    if (override_port > 0) {
        cfg.server.port = override_port;
        std::cout << "Port override: " << override_port << std::endl;
    }

    // ── 初始化日志 ──
    rpc::init_logger(cfg.log.dir, cfg.log.file,
                    spdlog::level::from_str(cfg.log.level));
    RPC_INFO("Logger initialized");

    // ── 创建线程池 ──
    size_t pool_size = cfg.server.thread_pool_size;
    rpc::ThreadPool pool(pool_size);
    RPC_INFO("Thread pool created, size={}", pool_size);

    // ── 创建 RPC 服务器 ──
    rpc::RpcServer server;
    server.set_thread_pool(&pool);

    // ── 注册服务 ──
    auto ping = std::make_shared<PingService>();
    ping->set_conn_counter([&server] { return server.connection_count(); });
    server.register_service(ping);
    RPC_INFO("Registered PingService (methods: Ping, Stats)");

    std::cout << "\nRegistered services:" << std::endl;
    for (auto& name : rpc::ServiceManager::instance().list_services()) {
        std::cout << "  - " << name << std::endl;
    }

    // ── 信号处理 ──
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // ── 启动服务器 ──
    std::cout << "\nStarting RPC server on 0.0.0.0:" << cfg.server.port << " ...\n"
              << "Press Ctrl+C to stop.\n" << std::endl;

    // 在后台线程运行服务器
    std::thread server_thread([&server, port = cfg.server.port]() {
        server.start(port);
    });

    // 主线程等待停止信号
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    // ── 优雅关闭 ──
    std::cout << "\nShutting down..." << std::endl;
    RPC_INFO("Server shutting down...");
    server.stop();

    if (server_thread.joinable()) {
        server_thread.join();
    }

    RPC_INFO("Server stopped. Goodbye.");
    std::cout << "Server stopped." << std::endl;
    return 0;
}
