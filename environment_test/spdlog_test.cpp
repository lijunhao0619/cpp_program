// ============================================================
// spdlog 常用操作教程
// 官方文档: https://github.com/gabime/spdlog
// ============================================================

#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/async.h>
#include <spdlog/fmt/bin_to_hex.h>
#include <memory>
#include <thread>

int main() {
    // ╔══════════════════════════════════════════════════════╗
    // ║  1. 基础日志（使用默认 logger）                     ║
    // ╚══════════════════════════════════════════════════════╝
    // spdlog 自带一个全局默认 logger，直接调用函数即可输出到控制台

    spdlog::info("Hello spdlog!");
    spdlog::warn("This is a warning");
    spdlog::error("Something went wrong: {}", 404);  // {} 占位符，类似 fmt 库语法
    spdlog::debug("Debug info: value={}", 3.14);

    // ╔══════════════════════════════════════════════════════╗
    // ║  2. 设置日志级别                                    ║
    // ╚══════════════════════════════════════════════════════╝
    // 级别从低到高: trace < debug < info < warn < error < critical < off
    // 设置后，低于该级别的日志会被忽略

    spdlog::set_level(spdlog::level::info);   // 只显示 info 及以上
    spdlog::debug("这条不会显示");             // debug < info，被过滤
    spdlog::info("这条正常显示");              // info >= info

    spdlog::set_level(spdlog::level::debug);  // 恢复显示所有

    // ╔══════════════════════════════════════════════════════╗
    // ║  3. 创建自定义 logger（控制台 + 彩色）               ║
    // ╚══════════════════════════════════════════════════════╝

    // 3a. 创建一个带颜色的控制台 logger
    auto console = spdlog::stdout_color_mt("console");
    console->info("This is a colored console logger");

    // 3b. 设置自定义格式
    // %^ %$ = 颜色起止, %Y-%m-%d = 日期, %T = 时间, %n = logger名
    // %l = 级别, %v = 日志正文
    console->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%n] %v");
    console->info("Custom pattern applied");

    // 3c. 针对不同场景设置不同 pattern
    console->set_pattern("%v");  // 只显示正文
    console->info("Short and clean message");

    // ╔══════════════════════════════════════════════════════╗
    // ║  4. 文件日志（多种 sink）                            ║
    // ╚══════════════════════════════════════════════════════╝

    // 4a. 基础文件日志 —— 写到一个文件
    auto file_logger = spdlog::basic_logger_mt("file_logger", "logs/basic.log");
    file_logger->info("Hello file!");

    // 4b. 按大小滚动 —— 单个文件超过 1MB 自动切分，保留 3 个旧文件
    auto rotating = spdlog::rotating_logger_mt("rotating",
        "logs/rotating.log", 1024 * 1024, 3);
    rotating->info("This goes to rotating file");

    // 4c. 按天滚动 —— 每天凌晨自动新建文件，保留 7 天
    auto daily = spdlog::daily_logger_mt("daily", "logs/daily.log", 0, 0);
    daily->info("Daily log entry");

    // ╔══════════════════════════════════════════════════════╗
    // ║  5. 多 sink 组合（同时输出到控制台 + 文件）         ║
    // ╚══════════════════════════════════════════════════════╝
    // 实用场景：本地开发时屏幕看 + 文件留存

    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();// 控制台 sink
    auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("logs/combined.log");// 文件 sink

    spdlog::sinks_init_list sink_list = {console_sink, file_sink};
    auto multi = std::make_shared<spdlog::logger>("multi", sink_list);
    multi->set_level(spdlog::level::info);
    spdlog::register_logger(multi);

    multi->info("这条既显示在屏幕，又写入文件");
    multi->warn("双重输出，方便调试");

    // ╔══════════════════════════════════════════════════════╗
    // ║  6. 格式化输出                                      ║
    // ╚══════════════════════════════════════════════════════╝
    // spdlog 使用 fmt 库做格式化，{} 占位符自动推断类型

    int port = 8080;
    std::string host = "127.0.0.1";
    double latency = 12.5;

    // 6a. 基本占位符 —— 按位置替换
    spdlog::info("Server listening on {}:{}", host, port);

    // 6b. 索引占位符 —— 指定参数位置，可重复使用
    spdlog::info("host={0}, port={1}, address={0}:{1}", host, port);

    // 6c. 浮点数精度控制
    spdlog::info("latency: {:.2f}ms", latency);   // 保留 2 位小数

    // 6d. 十六进制输出
    spdlog::info("port in hex: {:#x}", port);     // 0x1f90

    // 6e. 对齐和填充
    spdlog::info("{:<10} | {:>10}", "left", "right");   // 左对齐、右对齐

    // ╔══════════════════════════════════════════════════════╗
    // ║  7. 十六进制 dump（调试二进制数据的神器）            ║
    // ╚══════════════════════════════════════════════════════╝

    std::vector<uint8_t> packet = {0x00, 0x01, 0xAB, 0xCD, 0xEF, 0xFF};
    spdlog::info("Packet hex: {:n}", spdlog::to_hex(packet));     // 紧凑
    spdlog::info("Packet dump:\n{:a}", spdlog::to_hex(packet));   // 详细格式

    // ╔══════════════════════════════════════════════════════╗
    // ║  8. 异步日志（高性能场景）                           ║
    // ╚══════════════════════════════════════════════════════╝
    // 日志写入在后台线程，不阻塞业务逻辑

    // 8a. 全局开启异步模式（需在创建任何 logger 之前调用）
    // spdlog::init_thread_pool(8192, 1);   // 队列大小 8192，1 个后台线程
    // auto async_logger = spdlog::basic_logger_mt<spdlog::async_factory>("async", "logs/async.log");

    // 8b. 对于已有的 logger，可以直接创建异步版本
    auto async_file = spdlog::create_async<spdlog::sinks::basic_file_sink_mt>(
        "async_logger", "logs/async.log");
    async_file->info("Async log: this does not block the caller");

    // ╔══════════════════════════════════════════════════════╗
    // ║  9. flush 刷新 —— 确保日志落盘                      ║
    // ╚══════════════════════════════════════════════════════╝

    // 9a. 手动刷新
    file_logger->flush();          // 立即把缓冲写到磁盘

    // 9b. 设置自动刷新级别 —— 遇到 error 及以上自动 flush
    file_logger->flush_on(spdlog::level::err);
    file_logger->error("这条写入后会立刻 flush");   // 自动刷新
    file_logger->info("这条不会自动 flush");

    // 9c. 设置定时刷新间隔
    // spdlog::flush_every(std::chrono::seconds(3));  // 每 3 秒全局自动 flush

    // ╔══════════════════════════════════════════════════════╗
    // ║  10. 通过 logger 名字获取 / 管理                    ║
    // ╚══════════════════════════════════════════════════════╝

    // 10a. spdlog::get() 通过名字获取已注册的 logger
    auto same = spdlog::get("console");
    if (same) {
        same->info("Got logger by name 'console'");
    }

    // 10b. 删除（注销）一个 logger
    spdlog::drop("console");      // 释放资源

    // 10c. 删除所有 logger
    // spdlog::drop_all();

    // 10d. set_default_logger 替换默认 logger
    spdlog::set_default_logger(file_logger);
    spdlog::info("Now the default logger writes to file!");

    // ╔══════════════════════════════════════════════════════╗
    // ║  11. 多线程场景                                     ║
    // ╚══════════════════════════════════════════════════════╝
    // spdlog 默认是线程安全的，多线程直接写无需加锁

    std::thread t1([] {
        for (int i = 0; i < 3; i++) {
            spdlog::info("Thread1: iteration {}", i);
        }
    });
    std::thread t2([] {
        for (int i = 0; i < 3; i++) {
            spdlog::warn("Thread2: iteration {}", i);
        }
    });
    t1.join(); 
    t2.join();

    // ╔══════════════════════════════════════════════════════╗
    // ║  12. RPC 场景实战 —— 请求日志                       ║
    // ╚══════════════════════════════════════════════════════╝

    // 创建一个 RPC 专用 logger（彩色控制台 + 文件）
    auto rpc_console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto rpc_file = std::make_shared<spdlog::sinks::basic_file_sink_mt>("logs/rpc.log");
    auto rpc_log = std::make_shared<spdlog::logger>("rpc",
        spdlog::sinks_init_list{rpc_console, rpc_file});
    rpc_log->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [thread %t] %v");
    rpc_log->set_level(spdlog::level::info);

    // 模拟一次 RPC 调用
    std::string service = "MathService";
    std::string method = "Add";
    int request_id = 1001;
    auto start = std::chrono::steady_clock::now();

    rpc_log->info(">>> REQ  id={} service={}.{} params=[1,2,3]",
                  request_id, service, method);

    // ... 模拟处理 ...
    std::this_thread::sleep_for(std::chrono::milliseconds(15));

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    rpc_log->info("<<< RESP id={} result=6 elapsed={}ms", request_id, elapsed);

    // ╔══════════════════════════════════════════════════════╗
    // ║  13. Source location —— 自动记录文件名和行号       ║
    // ╚══════════════════════════════════════════════════════╝

    // 使用带 %s %# 的 pattern 自动添加源码位置
    rpc_log->set_pattern("[%H:%M:%S] [%^%l%$] [%s:%#] %v");
    rpc_log->info("这条日志自动附带文件名和行号");
    SPDLOG_LOGGER_INFO(rpc_log, "也可以用宏来输出");  // 宏自动捕获源码位置

    // ╔══════════════════════════════════════════════════════╗
    // ║  14. 关闭 & 清理                                    ║
    // ╚══════════════════════════════════════════════════════╝
    // 程序退出前调用，确保所有缓冲写入磁盘
    spdlog::shutdown();

    return 0;
}
