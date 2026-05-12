#include "conn_balancer.h"

#include <algorithm>
#include <random>
#include <stdexcept>

namespace rpc {

// ============================================================
// RoundRobinStrategy
// ============================================================

Endpoint RoundRobinStrategy::select(const std::vector<Endpoint>& nodes) {
    if (nodes.empty())
        throw std::runtime_error("RoundRobin: no nodes available");
    size_t idx = counter_++ % nodes.size();
    return nodes[idx];
}

// ============================================================
// RandomStrategy
// ============================================================

Endpoint RandomStrategy::select(const std::vector<Endpoint>& nodes) {
    if (nodes.empty())
        throw std::runtime_error("Random: no nodes available");

    static thread_local std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<size_t> dist(0, nodes.size() - 1);
    return nodes[dist(rng)];
}

// ============================================================
// WeightedStrategy
// ============================================================

Endpoint WeightedStrategy::select(const std::vector<Endpoint>& nodes) {
    if (nodes.empty())
        throw std::runtime_error("Weighted: no nodes available");

    // 计算总权重
    int total = 0;
    for (auto& n : nodes) total += n.weight;

    if (total <= 0)
        throw std::runtime_error("Weighted: total weight <= 0");

    // 按权重随机选择
    static thread_local std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(0, total - 1);
    int r = dist(rng);

    int accum = 0;
    for (auto& n : nodes) {
        accum += n.weight;
        if (r < accum) return n;
    }
    return nodes.back(); // fallback
}

// ============================================================
// LoadBalancer
// ============================================================

LoadBalancer::LoadBalancer()
    : strategy_(std::make_unique<RoundRobinStrategy>()) {}

void LoadBalancer::set_strategy(std::unique_ptr<BalancerStrategy> s) {
    if (s) strategy_ = std::move(s);
}

void LoadBalancer::set_nodes(std::vector<Endpoint> nodes) {
    nodes_ = std::move(nodes);
    reset();
}

Endpoint LoadBalancer::select() {
    return strategy_->select(nodes_);
}

const Endpoint* LoadBalancer::find(const std::string& name) const {
    for (auto& n : nodes_) {
        if (n.name == name) return &n;
    }
    return nullptr;
}

void LoadBalancer::add_node(const Endpoint& ep) {
    nodes_.push_back(ep);
}

bool LoadBalancer::remove_node(const std::string& name) {
    auto it = std::find_if(nodes_.begin(), nodes_.end(),
        [&](const Endpoint& e) { return e.name == name; });
    if (it == nodes_.end()) return false;
    nodes_.erase(it);
    return true;
}

void LoadBalancer::sync_nodes(std::vector<Endpoint> nodes) {
    nodes_ = std::move(nodes);
    reset();
}

std::string LoadBalancer::strategy_name() const {
    return strategy_->name();
}

void LoadBalancer::reset() {
    // 每次换列表重置轮询计数器：重建策略对象
    auto name = strategy_->name();
    set_strategy(create_strategy(name));
}

// ============================================================
// Factory
// ============================================================

std::unique_ptr<BalancerStrategy> create_strategy(const std::string& name) {
    if (name == "random")   return std::make_unique<RandomStrategy>();
    if (name == "weighted") return std::make_unique<WeightedStrategy>();
    return std::make_unique<RoundRobinStrategy>(); // default
}

} // namespace rpc
