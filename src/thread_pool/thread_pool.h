#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <stop_token>
#include <thread>
#include <type_traits>
#include <vector>

namespace rpc {

class ThreadPool {
public:
    explicit ThreadPool(size_t num_threads = 0);

    ~ThreadPool();

    // 禁止拷贝和移动
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    // 提交任务，返回 future
    template <typename F, typename... Args>
    auto enqueue(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>>;

    // 等待所有已提交的任务完成
    void drain();

    // 获取统计信息
    size_t active_count() const { return num_threads_; }
    size_t pending_count() const { return pending_count_.load(); }
    size_t total_completed() const { return completed_.load(); }

private:
    void worker_loop();

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    mutable std::mutex mux_;
    std::condition_variable cv_;
    std::condition_variable drain_cv_;

    std::atomic<size_t> pending_count_{0};
    std::atomic<size_t> active_tasks_{0};
    std::atomic<uint64_t> completed_{0};
    size_t num_threads_;

    bool stop_ = false;
};

// ── 模板实现在头文件 ──

template <typename F, typename... Args>
auto ThreadPool::enqueue(F&& f, Args&&... args)
    -> std::future<std::invoke_result_t<F, Args...>> {

    using return_type = std::invoke_result_t<F, Args...>;

    // 用 packaged_task 包装，返回 future
    auto task = std::make_shared<std::packaged_task<return_type()>>(
        [func = std::forward<F>(f),
         ... params = std::forward<Args>(args)]() mutable {
            return func(std::move(params)...);
        });

    std::future<return_type> result = task->get_future();

    {
        std::lock_guard<std::mutex> lk(mux_);
        if (stop_) {
            throw std::runtime_error("ThreadPool: enqueue on stopped pool");
        }
        tasks_.emplace([task]() { (*task)(); });
        pending_count_.fetch_add(1);
    }
    cv_.notify_one();

    return result;
}

} // namespace rpc
