#pragma once

#include <boost/asio/io_context.hpp>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace zk_client {
class zk_client;
}

namespace rpc {

// 服务实例信息
struct ServiceInstance {
    std::string name;      // ZK 节点名
    std::string address;   // IP:Port 地址
    int64_t     ctime = 0; // 创建时间 (ZK epoch)
    int64_t     mtime = 0; // 修改时间
    int32_t     version = 0;
    int32_t     data_length = 0;
};

// ZK 服务注册与发现
class ServiceRegistry {
public:
    explicit ServiceRegistry(boost::asio::io_context& ios);
    ~ServiceRegistry();

    // 连接 ZK
    bool connect(const std::string& host = "127.0.0.1",
                 uint16_t port = 2181,
                 int32_t timeout_ms = 4000);

    // 列出指定路径下的所有子节点
    std::vector<std::string> get_children(const std::string& path);

    // 列出所有已注册的服务实例
    std::vector<ServiceInstance> list_services(
        const std::string& services_path = "/rpc/services");

    // 获取特定节点的数据（文本）
    std::string get_node_data(const std::string& path);

    // 获取特定服务实例的详细信息
    ServiceInstance get_service(const std::string& node_path);

    // 判断节点是否存在
    bool node_exists(const std::string& path);

    // 注册服务（创建临时节点）
    bool register_service(const std::string& services_path,
                          const std::string& node_name,
                          const std::string& address);

    // 注销服务（删除节点）
    bool unregister_service(const std::string& node_path);

    // 创建持久节点（用于初始化目录结构）
    bool create_persistent_node(const std::string& path);

private:
    // 推进 io_context 直到 future 就绪
    void drive_io_context();

    boost::asio::io_context& ios_;
    std::shared_ptr<zk_client::zk_client> client_;
    bool connected_ = false;
};

} // namespace rpc
