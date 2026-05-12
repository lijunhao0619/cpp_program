#include "log_init.h"

#include <filesystem>
#include <mutex>

namespace rpc {

namespace {

// 全局 logger 实例 + 初始化保护
std::shared_ptr<spdlog::logger> g_logger;
std::once_flag g_init_flag;

} // namespace

void init_logger(const std::string& log_dir,
                 const std::string& log_file,
                 spdlog::level::level_enum level) {
    std::call_once(g_init_flag, [&] {
        std::filesystem::create_directories(log_dir);

        // 控制台 sink（彩色，开发调试用）
        auto console_sink =
            std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console_sink->set_level(level);

        // 文件 sink（按大小滚动：10MB × 5 个文件 = 50MB 上限）
        auto log_path = log_dir + "/" + log_file;
        auto file_sink =
            std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                log_path, 1024 * 1024 * 10, 5);
        file_sink->set_level(level);

        g_logger = std::make_shared<spdlog::logger>(
            "rpc", spdlog::sinks_init_list{console_sink, file_sink});
        g_logger->set_level(level);

        // 格式：[日期 时间] [级别] [线程ID] 消息
        g_logger->set_pattern(
            "[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] %v");

        // error 及以上自动 flush，防止崩溃丢失关键日志
        g_logger->flush_on(spdlog::level::err);

        spdlog::register_logger(g_logger);
    });
}

std::shared_ptr<spdlog::logger> get_logger() {
    if (!g_logger) {
        // 未显式初始化时使用默认配置
        init_logger();
    }
    return g_logger;
}

void set_log_level(spdlog::level::level_enum level) {
    get_logger()->set_level(level);
}

} // namespace rpc
