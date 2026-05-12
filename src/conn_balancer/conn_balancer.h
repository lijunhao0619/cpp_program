#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace rpc {

// 负载均衡器返回的节点信息
struct Endpoint {
    std::string name;    // 节点名
    std::string address; // IP:Port
    int         weight = 1; // 权重（加权策略使用）
};

// ── 策略接口 ──
class BalancerStrategy {
public:
    virtual ~BalancerStrategy() = default;
    virtual Endpoint select(const std::vector<Endpoint>& nodes) = 0;
    virtual std::string name() const = 0;
};

// ── 轮询策略 ──
class RoundRobinStrategy : public BalancerStrategy {
public:
    Endpoint select(const std::vector<Endpoint>& nodes) override;
    std::string name() const override { return "round_robin"; }
private:
    uint64_t counter_ = 0;
};

// ── 随机策略 ──
class RandomStrategy : public BalancerStrategy {
public:
    Endpoint select(const std::vector<Endpoint>& nodes) override;
    std::string name() const override { return "random"; }
};

// ── 加权策略 ──
class WeightedStrategy : public BalancerStrategy {
public:
    Endpoint select(const std::vector<Endpoint>& nodes) override;
    std::string name() const override { return "weighted"; }
};

// ── 负载均衡器 ──
class LoadBalancer {
public:
    LoadBalancer();

    // 设置策略（默认 round_robin）
    void set_strategy(std::unique_ptr<BalancerStrategy> strategy);

    // 设置节点列表（来自 ZK 或其他来源）
    void set_nodes(std::vector<Endpoint> nodes);

    // 获取当前节点列表
    const std::vector<Endpoint>& nodes() const { return nodes_; }

    // 选择一个节点
    Endpoint select();

    // 按名称查找节点
    const Endpoint* find(const std::string& name) const;

    // 添加/移除节点
    void add_node(const Endpoint& ep);
    bool remove_node(const std::string& name);

    // 批量同步（替换全部节点）
    void sync_nodes(std::vector<Endpoint> nodes);

    // 当前策略名
    std::string strategy_name() const;

    // 重置轮询计数器
    void reset();

private:
    std::unique_ptr<BalancerStrategy> strategy_;
    std::vector<Endpoint> nodes_;
};

// ── 工厂函数 ──
std::unique_ptr<BalancerStrategy> create_strategy(const std::string& name);

} // namespace rpc
