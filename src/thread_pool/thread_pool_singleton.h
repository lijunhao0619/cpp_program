#pragma once

#include "thread_pool.h"

#include <memory>
#include <mutex>

namespace rpc {

// 全局线程池单例
class ThreadPoolSingleton {
public:
    // 初始化（未初始化时自动调用）
    static void init(size_t num_threads = 0);

    // 获取单例实例
    static ThreadPool& instance();

    // 销毁单例
    static void shutdown();

    // 禁止实例化
    ThreadPoolSingleton() = delete;

private:
    static std::unique_ptr<ThreadPool> pool_;
    static std::once_flag init_flag_;
};

} // namespace rpc
