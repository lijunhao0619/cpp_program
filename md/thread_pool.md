# 线程池模块设计文档

## 概述

固定大小的生产者-消费者线程池，用于 RPC 服务端并发处理请求，避免频繁创建销毁线程。

## 文件位置

| 文件 | 用途 |
|------|------|
| `src/thread_pool/thread_pool.h` | `ThreadPool` 类声明 + `enqueue` 模板 |
| `src/thread_pool/thread_pool.cpp` | 线程池生命周期实现 |
| `src/thread_pool/thread_pool_singleton.h` | 全局单例声明 |
| `src/thread_pool/thread_pool_singleton.cpp` | 单例实现 |

---

## 架构设计

### 生产者-消费者模型

```
                    ┌──────────────────┐
  enqueue(task) ──▶ │   task queue     │
                    │ std::function... │
                    └──────┬───────────┘
                           │
              ┌────────────┼────────────┐
              ▼            ▼            ▼
         ┌────────┐  ┌────────┐  ┌────────┐
         │Worker 1│  │Worker 2│  │Worker N│
         └───┬────┘  └───┬────┘  └───┬────┘
             │            │            │
             ▼            ▼            ▼
         std::future   std::future   std::future
```

- `enqueue()` 将任务包装为 `packaged_task`，推入共享队列
- worker 线程持有 `condition_variable` 等待，取出任务执行
- 调用方通过 `std::future` 获取结果或异常

### 线程模型

```
ThreadPool(num_threads)
    │
    ├── ctor: 创建 N 个 worker 线程
    │    每个 worker: while(true) { wait(cv); pop; execute; notify(drain); }
    │
    ├── enqueue: lock → push → notify_one
    │
    ├── drain: wait until queue empty && active_tasks == 0
    │
    └── dtor: stop=true → notify_all → join all → report stats
```

---

## API 设计

### ThreadPool

```cpp
// 创建线程池（0 = 自动检测 hardware_concurrency）
explicit ThreadPool(size_t num_threads = 0);

// 提交任务，返回 future
template <typename F, typename... Args>
auto enqueue(F&& f, Args&&... args)
    -> std::future<std::invoke_result_t<F, Args...>>;

// 等待所有已提交任务完成
void drain();

// 统计信息
size_t active_count() const;    // worker 线程数
size_t pending_count() const;   // 队列中等待的任务数
size_t total_completed() const; // 已完成任务总数
```

### enqueue 用法

```cpp
ThreadPool pool(8);

// 无返回值任务
auto f1 = pool.enqueue([] { do_work(); });

// 有返回值
auto f2 = pool.enqueue([](int a, int b) { return a + b; }, 10, 20);
int result = f2.get(); // 30

// 异常传播
auto f3 = pool.enqueue([] { throw std::runtime_error("boom"); });
f3.get(); // 抛出 runtime_error
```

### ThreadPoolSingleton

```cpp
// 初始化（首次调用 enqueue 时自动 init）
ThreadPoolSingleton::init(8);

// 获取实例
ThreadPool& pool = ThreadPoolSingleton::instance();
pool.enqueue([] { /* ... */ });

// 销毁
ThreadPoolSingleton::shutdown();
```

---

## 实现要点

### 1. 任务封装（packaged_task + 类型擦除）

```cpp
template <typename F, typename... Args>
auto ThreadPool::enqueue(F&& f, Args&&... args)
    -> std::future<std::invoke_result_t<F, Args...>> {

    using return_type = std::invoke_result_t<F, Args...>;

    auto task = std::make_shared<std::packaged_task<return_type()>>(
        [func = std::forward<F>(f),
         ... params = std::forward<Args>(args)]() mutable {
            return func(std::move(params)...);
        });

    std::future<return_type> result = task->get_future();
    // push into queue...
    return result;
}
```

`packaged_task` 提供 `get_future()` 和可调用对象包装，`shared_ptr` 确保任务在队列中存活直到被 worker 执行。

### 2. 优雅关闭

析构函数：
1. 置 `stop_ = true`
2. `cv_.notify_all()` 唤醒全部 worker
3. 每个 worker 检测 `stop_ && tasks_.empty()` → 退出循环
4. `join()` 等待所有 worker 完成

已在队列中的任务保证执行完毕；不再接受新任务。

### 3. drain 机制

```cpp
void ThreadPool::drain() {
    drain_cv_.wait(lk, [this] {
        return tasks_.empty() && active_tasks_.load() == 0;
    });
}
```

独立的 `drain_cv_` 条件变量，每次任务完成后 `notify_one()`，避免与 worker 唤醒竞争。

### 4. 线程安全

| 操作 | 保护机制 |
|------|----------|
| 入队 | `std::lock_guard` + `notify_one` |
| 出队 | `std::unique_lock` + `wait` |
| 计数器 | `std::atomic`（无锁） |
| 初始化 | `std::call_once`（单例） |

---

## 测试结果

| 测试 | 内容 | 结果 |
|------|------|------|
| Test 1 | 基本 enqueue + future 返回值 | `f1=42 f2=30` ✅ |
| Test 2 | 100 任务并发（8 线程） | 217ms, sum=328350 ✅ |
| Test 3 | drain() 等待完成 | pending=0 ✅ |
| Test 4 | 默认线程数（24 核） | counter=50 ✅ |
| Test 5 | Singleton 模式 | 正确返回 ✅ |
| Test 6 | 异常传播 | caught "task error" ✅ |

---

## 编译依赖

- C++20（`std::invoke_result_t`、模板参数包展开）
- 纯标准库，无外部依赖

## 使用示例

### RPC 服务端集成

```cpp
// server_main.cpp
rpc::ThreadPoolSingleton::init(8);
auto& pool = rpc::ThreadPoolSingleton::instance();

// 收到每个请求后提交到线程池
void on_request(const RpcRequest& req) {
    pool.enqueue([req] {
        auto response = handle(req);
        send(response);
    });
}
```
