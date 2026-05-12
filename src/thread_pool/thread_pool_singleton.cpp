#include "thread_pool_singleton.h"

namespace rpc {

std::unique_ptr<ThreadPool> ThreadPoolSingleton::pool_;
std::once_flag ThreadPoolSingleton::init_flag_;

void ThreadPoolSingleton::init(size_t num_threads) {
    std::call_once(init_flag_, [num_threads] {
        pool_ = std::make_unique<ThreadPool>(num_threads);
    });
}

ThreadPool& ThreadPoolSingleton::instance() {
    init(); // 未初始化时用默认参数
    return *pool_;
}

void ThreadPoolSingleton::shutdown() {
    pool_.reset();
}

} // namespace rpc
