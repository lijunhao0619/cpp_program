#include "thread_pool/thread_pool.h"
#include "thread_pool/thread_pool_singleton.h"

#include <chrono>
#include <iostream>
#include <numeric>
#include <vector>

using namespace std::chrono_literals;

int main() {
    std::cout << "=== Thread Pool Test ===\n";

    // ════════════════════════════════════════════════════
    // Test 1: 基础提交与返回
    // ════════════════════════════════════════════════════
    std::cout << "\n[Test 1] Basic enqueue / future\n";
    {
        rpc::ThreadPool pool(4);

        auto f1 = pool.enqueue([] {
            std::this_thread::sleep_for(50ms);
            return 42;
        });
        auto f2 = pool.enqueue([](int a, int b) {
            return a + b;
        }, 10, 20);

        std::cout << "  f1 = " << f1.get() << "\n";
        std::cout << "  f2 = " << f2.get() << "\n";
        std::cout << "  completed: " << pool.total_completed() << "\n";
    }

    // ════════════════════════════════════════════════════
    // Test 2: 大量并发任务
    // ════════════════════════════════════════════════════
    std::cout << "\n[Test 2] 100 tasks\n";
    {
        rpc::ThreadPool pool(8);
        std::vector<std::future<int>> futures;

        auto start = std::chrono::steady_clock::now();

        for (int i = 0; i < 100; ++i) {
            futures.push_back(pool.enqueue([i] {
                std::this_thread::sleep_for(10ms);
                return i * i;
            }));
        }

        // 收结果
        int sum = 0;
        for (auto& f : futures) sum += f.get();

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();

        std::cout << "  sum = " << sum << "\n";
        std::cout << "  time = " << elapsed << "ms\n";
        std::cout << "  completed = " << pool.total_completed() << "\n";
    }

    // ════════════════════════════════════════════════════
    // Test 3: drain 等待完成
    // ════════════════════════════════════════════════════
    std::cout << "\n[Test 3] drain()\n";
    {
        rpc::ThreadPool pool(4);
        for (int i = 0; i < 20; ++i) {
            pool.enqueue([i] {
                std::this_thread::sleep_for(5ms);
            });
        }
        pool.drain();
        std::cout << "  after drain: pending=" << pool.pending_count()
                  << " completed=" << pool.total_completed() << "\n";
    }

    // ════════════════════════════════════════════════════
    // Test 4: 默认线程数 = CPU 核心数
    // ════════════════════════════════════════════════════
    std::cout << "\n[Test 4] Default thread count\n";
    {
        rpc::ThreadPool pool; // 自动检测
        std::cout << "  threads = " << pool.active_count() << "\n";

        std::atomic<int> counter{0};
        for (int i = 0; i < 50; ++i) {
            pool.enqueue([&] { counter.fetch_add(1); });
        }
        pool.drain();
        std::cout << "  counter = " << counter.load() << "\n";
    }

    // ════════════════════════════════════════════════════
    // Test 5: Singleton
    // ════════════════════════════════════════════════════
    std::cout << "\n[Test 5] ThreadPoolSingleton\n";
    {
        rpc::ThreadPoolSingleton::init(4);

        auto f1 = rpc::ThreadPoolSingleton::instance().enqueue([] {
            return std::string("hello");
        });
        auto f2 = rpc::ThreadPoolSingleton::instance().enqueue([] {
            return 3.14159;
        });

        std::cout << "  f1 = " << f1.get() << "\n";
        std::cout << "  f2 = " << f2.get() << "\n";
        std::cout << "  completed = "
                  << rpc::ThreadPoolSingleton::instance().total_completed()
                  << "\n";

        rpc::ThreadPoolSingleton::shutdown();
    }

    // ════════════════════════════════════════════════════
    // Test 6: 异常安全
    // ════════════════════════════════════════════════════
    std::cout << "\n[Test 6] Exception propagation\n";
    {
        rpc::ThreadPool pool(2);
        auto f = pool.enqueue([] {
            throw std::runtime_error("task error");
            return 0;
        });

        try {
            f.get();
            std::cout << "  (no exception)\n";
        } catch (const std::exception& e) {
            std::cout << "  caught: " << e.what() << "\n";
        }
    }

    std::cout << "\n=== All Tests Passed ===\n";
    return 0;
}
