#include "node_manager.h"
#include "service_registry.h"

#include <iostream>

namespace rpc {

NodeManager::NodeManager(boost::asio::io_context& ios)
    : ios_(ios)
    , registry_(std::make_unique<ServiceRegistry>(ios)) {}

NodeManager::~NodeManager() = default;

bool NodeManager::connect(const std::string& host,
                           uint16_t port,
                           int32_t timeout_ms) {
    return registry_->connect(host, port, timeout_ms);
}

void NodeManager::init_zk_paths(const std::string& services_path) {
    services_path_ = services_path;
    // 确保根路径存在
    auto pos = services_path.rfind('/');
    if (pos != std::string::npos && pos > 0) {
        std::string root = services_path.substr(0, pos);
        registry_->create_persistent_node(root);
    }
    registry_->create_persistent_node(services_path);
}

std::vector<Endpoint> NodeManager::refresh_nodes() {
    nodes_.clear();
    auto services = registry_->list_services(services_path_);
    for (auto& svc : services) {
        Endpoint ep;
        ep.name    = svc.name;
        ep.address = svc.address;
        ep.weight  = 1; // 后续可从 address 解析或单独存储
        nodes_.push_back(std::move(ep));
    }
    balancer_.set_nodes(nodes_);
    std::cout << "[node_mgr] refreshed: " << nodes_.size()
              << " nodes from " << services_path_ << "\n";
    return nodes_;
}

Endpoint NodeManager::select_node() {
    // 如果无节点，尝试从 ZK 刷新
    if (nodes_.empty()) {
        refresh_nodes();
    }
    return balancer_.select();
}

void NodeManager::set_balancer_strategy(const std::string& name) {
    balancer_.set_strategy(create_strategy(name));
    // 同步节点列表到新策略
    balancer_.set_nodes(nodes_);
    std::cout << "[node_mgr] strategy set: " << name << "\n";
}

std::string NodeManager::strategy_name() const {
    return balancer_.strategy_name();
}

bool NodeManager::register_node(const std::string& node_name,
                                 const std::string& address,
                                 int weight) {
    (void)weight; // 后续可存入节点数据（JSON）
    return registry_->register_service(
        services_path_, node_name, address);
}

bool NodeManager::unregister_node(const std::string& node_name) {
    std::string full = services_path_ + "/" + node_name;
    return registry_->unregister_service(full);
}

bool NodeManager::node_exists(const std::string& node_path) {
    return registry_->node_exists(node_path);
}

std::vector<std::string> NodeManager::list_node_names() {
    return registry_->get_children(services_path_);
}

std::string NodeManager::get_node_data(const std::string& path) {
    return registry_->get_node_data(path);
}

void NodeManager::set_services_path(const std::string& path) {
    services_path_ = path;
}

} // namespace rpc
