#include "thread_pool.h"

#include <iostream>

namespace rpc {

ThreadPool::ThreadPool(size_t num_threads)
    : num_threads_(num_threads ? num_threads
                               : std::thread::hardware_concurrency()) {
    // 兜底：无法检测硬件线程数时用 4
    if (num_threads_ == 0) num_threads_ = 4;

    workers_.reserve(num_threads_);
    for (size_t i = 0; i < num_threads_; ++i) {
        workers_.emplace_back(&ThreadPool::worker_loop, this);
    }
    std::cout << "[thread_pool] started " << num_threads_
              << " worker threads\n";
}

ThreadPool::~ThreadPool() {
    {
        std::lock_guard<std::mutex> lk(mux_);
        stop_ = true;
    }
    cv_.notify_all();
    for (auto& w : workers_) {
        if (w.joinable()) w.join();
    }
    std::cout << "[thread_pool] stopped (completed "
              << completed_.load() << " tasks)\n";
}

void ThreadPool::worker_loop() {
    while (true) {
        std::function<void()> task;

        {
            std::unique_lock<std::mutex> lk(mux_);
            cv_.wait(lk, [this] { return stop_ || !tasks_.empty(); });

            if (stop_ && tasks_.empty()) return;

            task = std::move(tasks_.front());
            tasks_.pop();
        }

        active_tasks_.fetch_add(1);
        task();
        active_tasks_.fetch_sub(1);
        completed_.fetch_add(1);
        pending_count_.fetch_sub(1);

        // 通知 drain() 等待者
        drain_cv_.notify_one();
    }
}

void ThreadPool::drain() {
    std::unique_lock<std::mutex> lk(mux_);
    drain_cv_.wait(lk, [this] {
        return tasks_.empty() && active_tasks_.load() == 0;
    });
}

} // namespace rpc
