#pragma once

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <memory>
#include <string>

namespace rpc {

// 初始化日志系统（控制台彩色 + 文件滚动）
// log_dir:  日志目录，默认 "logs"
// log_file: 日志文件名，默认 "rpc.log"
// level:    最低输出级别，默认 info
void init_logger(
    const std::string& log_dir = "logs",
    const std::string& log_file = "rpc.log",
    spdlog::level::level_enum level = spdlog::level::info);

// 获取全局 logger 实例
std::shared_ptr<spdlog::logger> get_logger();

// 设置全局日志级别（运行时动态调整）
void set_log_level(spdlog::level::level_enum level);

} // namespace rpc

// ============================================================
// 宏定义 —— 使用宏自动捕获文件名、行号，无需每次传 logger
// ============================================================

#define RPC_TRACE(...)    ::rpc::get_logger()->trace(__VA_ARGS__)
#define RPC_DEBUG(...)    ::rpc::get_logger()->debug(__VA_ARGS__)
#define RPC_INFO(...)     ::rpc::get_logger()->info(__VA_ARGS__)
#define RPC_WARN(...)     ::rpc::get_logger()->warn(__VA_ARGS__)
#define RPC_ERROR(...)    ::rpc::get_logger()->error(__VA_ARGS__)
#define RPC_CRITICAL(...) ::rpc::get_logger()->critical(__VA_ARGS__)

// 带条件的日志（避免不必要的格式化开销）
#define RPC_LOG_TRACE_ENABLED (::rpc::get_logger()->should_log(spdlog::level::trace))
#define RPC_LOG_DEBUG_ENABLED (::rpc::get_logger()->should_log(spdlog::level::debug))
