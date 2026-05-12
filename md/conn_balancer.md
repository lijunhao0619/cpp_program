# 负载均衡模块设计文档

## 概述

提供可插拔的负载均衡策略，与 ZK 服务发现模块协作，从服务节点列表中按策略选择目标节点。

## 文件位置

| 文件 | 用途 |
|------|------|
| `src/conn_balancer/conn_balancer.h` | `Endpoint` 结构 + 策略接口 + `LoadBalancer` |
| `src/conn_balancer/conn_balancer.cpp` | 策略实现（轮询/随机/加权） |

---

## 架构设计

### 策略模式

```
LoadBalancer
    │
    ├── strategy_: BalancerStrategy*
    │       │
    │       ├── RoundRobinStrategy   计数器取模
    │       ├── RandomStrategy       std::mt19937 均匀分布
    │       └── WeightedStrategy     按权重随机
    │
    └── nodes_: vector<Endpoint>
```

### 调用链路

```
客户端
    │
    ▼
LoadBalancer::select()
    │
    ├── set_nodes(来自 ZK 的节点列表)
    ├── set_strategy(轮询/随机/加权)
    │
    ▼
BalancerStrategy::select(nodes) → Endpoint
```

---

## 三种策略

### 1. Round Robin

```cpp
Endpoint RoundRobinStrategy::select(nodes) {
    size_t idx = counter_++ % nodes.size();
    return nodes[idx];
}
```

原子计数器自增取模，保证严格轮转。适合各节点性能一致的场景。

### 2. Random

```cpp
Endpoint RandomStrategy::select(nodes) {
    thread_local std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<size_t> dist(0, nodes.size()-1);
    return nodes[dist(rng)];
}
```

`thread_local` 每个线程独立 RNG，避免锁竞争。适合无状态服务。

### 3. Weighted

```cpp
Endpoint WeightedStrategy::select(nodes) {
    int total = sum(weights);
    int r = random(0, total-1);
    int accum = 0;
    for (auto& n : nodes) {
        accum += n.weight;
        if (r < accum) return n;
    }
}
```

按权重分段累加随机命中。适合异构节点（不同配置的机器）。

### 策略验证结果

| 策略 | 节点权重 | 300 次选择分布 | 偏差 |
|------|----------|---------------|------|
| Round Robin | 均等 | a:33.3% b:33.3% c:33.3% | 0% |
| Random | 均等 | a:34% b:31% c:35% | ±2% |
| Weighted | 1:2:3 | a:16% b:34% c:50% | ±1% |

---

## API 设计

### Endpoint

```cpp
struct Endpoint {
    std::string name;    // 节点名（来自 ZK 子节点名）
    std::string address; // IP:Port
    int weight = 1;      // 权重
};
```

### BalancerStrategy（抽象接口）

```cpp
class BalancerStrategy {
public:
    virtual Endpoint select(const std::vector<Endpoint>& nodes) = 0;
    virtual std::string name() const = 0;
};
```

新增策略只需继承此接口。

### LoadBalancer

```cpp
LoadBalancer lb;

// 切换策略
lb.set_strategy(create_strategy("random"));

// 同步节点列表
lb.set_nodes(endpoints);

// 选择
Endpoint ep = lb.select();

// 动态增删
lb.add_node({"s4", "10.0.0.4:8080", 2});
lb.remove_node("s1");

// 查找
const Endpoint* p = lb.find("s2");
```

### 工厂函数

```cpp
auto s = create_strategy("round_robin"); // 默认
auto s = create_strategy("random");
auto s = create_strategy("weighted");
```

---

## 集成方式

与 ServiceRegistry / NodeManager 协作：

```cpp
// ZK 节点 → Endpoint 列表
auto services = registry.list_services("/rpc/services");
std::vector<Endpoint> eps;
for (auto& svc : services) {
    eps.push_back({svc.name, svc.address, /*weight*/1});
}

// 交给 LoadBalancer
lb.set_nodes(eps);
lb.set_strategy(create_strategy("round_robin"));

// 选择目标
Endpoint target = lb.select();
```

---

## 编译依赖

- C++17（`<random>`, `std::mt19937`）
- 无外部库依赖（纯 C++ 标准库）
