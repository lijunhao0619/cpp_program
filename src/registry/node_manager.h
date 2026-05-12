#pragma once

#include "conn_balancer/conn_balancer.h"

#include <boost/asio/io_context.hpp>
#include <memory>
#include <string>
#include <vector>

namespace rpc {

class ServiceRegistry;

// ZK 节点管理器 = ServiceRegistry + LoadBalancer
class NodeManager {
public:
    explicit NodeManager(boost::asio::io_context& ios);
    ~NodeManager();

    // 连接 ZK
    bool connect(const std::string& host = "127.0.0.1",
                 uint16_t port = 2181,
                 int32_t timeout_ms = 4000);

    // 初始化 ZK 路径结构
    void init_zk_paths(const std::string& services_path = "/rpc/services");

    // 从 ZK 刷新节点列表
    std::vector<Endpoint> refresh_nodes();

    // 获取缓存的服务列表
    const std::vector<Endpoint>& nodes() const { return nodes_; }

    // 使用当前均衡策略选择一个节点
    Endpoint select_node();

    // 切换均衡策略
    void set_balancer_strategy(const std::string& strategy_name);

    // 当前策略名
    std::string strategy_name() const;

    // 注册一个服务
    bool register_node(const std::string& node_name,
                       const std::string& address,
                       int weight = 1);

    // 注销一个服务
    bool unregister_node(const std::string& node_name);

    // 节点是否存在
    bool node_exists(const std::string& node_path);

    // 列出 ZK 子节点名
    std::vector<std::string> list_node_names();

    // 获取指定节点的数据
    std::string get_node_data(const std::string& path);

    // 设置服务注册路径
    void set_services_path(const std::string& path);

private:
    std::string services_path_ = "/rpc/services";
    boost::asio::io_context& ios_;
    std::unique_ptr<ServiceRegistry> registry_;
    LoadBalancer balancer_;
    std::vector<Endpoint> nodes_;
};

} // namespace rpc
