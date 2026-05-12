#include "load_config/log_init.h"
#include <thread>

int main() {
    // 1. 零初始化直接用
    RPC_INFO("=== Log Module Test ===");

    // 2. 六个级别
    RPC_TRACE("trace message");
    RPC_DEBUG("debug: value={}", 42);
    RPC_INFO("info: server started on port {}", 8080);
    RPC_WARN("warn: retry {}/{}", 3, 10);
    RPC_ERROR("error: connection refused, code={}", 500);
    RPC_CRITICAL("critical: out of memory");

    // 3. 运行时切换级别
    rpc::set_log_level(spdlog::level::warn);
    RPC_INFO("这条 info 不会显示 (当前级别 warn)");
    RPC_WARN("这条 warn 正常显示");

    // 4. 多线程
    rpc::set_log_level(spdlog::level::info);
    std::thread t1([] {
        RPC_INFO("Thread1 working");
    });
    std::thread t2([] {
        RPC_WARN("Thread2 warning");
    });
    t1.join();
    t2.join();

    // 5. 条件检查
    if (RPC_LOG_TRACE_ENABLED) {
        RPC_TRACE("trace 开启时会显示");
    }

    RPC_INFO("=== Test Complete ===");
    spdlog::shutdown();
    return 0;
}
