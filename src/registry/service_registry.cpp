#include "service_registry.h"
#include "zk_client/zk_client.hpp"

#include <chrono>
#include <iostream>

namespace rpc {

using zk_result = zk_client::result<void>;
using zk_children_r = zk_client::result<zk_client::get_children_response>;
using zk_data_r = zk_client::result<zk_client::get_data_response>;
using zk_exists_r = zk_client::result<zk_client::exists_response>;

ServiceRegistry::ServiceRegistry(boost::asio::io_context& ios)
    : ios_(ios)
    , client_(std::make_shared<zk_client::zk_client>(ios)) {}

ServiceRegistry::~ServiceRegistry() = default;

// 轮询 io_context 直到 future 完成
void ServiceRegistry::drive_io_context() {
    // 我们需要 io_context 在线程中运行
    // 如果当前线程还没运行 ios_.run()，则用 poll_one 推进
    while (ios_.poll_one()) {}
}

bool ServiceRegistry::connect(const std::string& host,
                               uint16_t port,
                               int32_t timeout_ms) {
    auto future = client_->connect(host, port, timeout_ms);

    // 把 io 线程交给调用方或内部线程处理
    // 此处使用 poll 机制：在 future 未完成期间持续推进 io
    using namespace std::chrono_literals;
    while (future.wait_for(10ms) == std::future_status::timeout) {
        ios_.poll_one();
    }

    auto result = future.get();
    connected_ = result.is_ok();
    if (!connected_) {
        std::cerr << "[registry] ZK connect failed: "
                  << result.err() << std::endl;
    }
    return connected_;
}

std::vector<std::string> ServiceRegistry::get_children(
    const std::string& path) {
    if (!connected_) return {};

    auto future = client_->get_children(path);

    using namespace std::chrono_literals;
    while (future.wait_for(10ms) == std::future_status::timeout) {
        ios_.poll_one();
    }

    auto result = future.get();
    if (result.is_err()) {
        std::cerr << "[registry] get_children(" << path
                  << ") error: " << result.err() << std::endl;
        return {};
    }
    return std::move(result.ok().children);
}

std::vector<ServiceInstance> ServiceRegistry::list_services(
    const std::string& services_path) {
    auto children = get_children(services_path);
    std::vector<ServiceInstance> services;

    for (const auto& name : children) {
        std::string full_path = services_path + "/" + name;
        services.push_back(get_service(full_path));
    }
    return services;
}

std::string ServiceRegistry::get_node_data(const std::string& path) {
    if (!connected_) return {};

    auto future = client_->get_data(path);

    using namespace std::chrono_literals;
    while (future.wait_for(10ms) == std::future_status::timeout) {
        ios_.poll_one();
    }

    auto result = future.get();
    if (result.is_err()) {
        std::cerr << "[registry] get_data(" << path
                  << ") error: " << result.err() << std::endl;
        return {};
    }
    auto& data = result.ok().data;
    return std::string(data.begin(), data.end());
}

ServiceInstance ServiceRegistry::get_service(const std::string& node_path) {
    ServiceInstance si;
    // 从路径中提取节点名
    auto pos = node_path.rfind('/');
    si.name = (pos != std::string::npos) ? node_path.substr(pos + 1) : node_path;

    if (!connected_) return si;

    auto future = client_->get_data(node_path);

    using namespace std::chrono_literals;
    while (future.wait_for(10ms) == std::future_status::timeout) {
        ios_.poll_one();
    }

    auto result = future.get();
    if (result.is_ok()) {
        si.address     = std::string(result.ok().data.begin(),
                                     result.ok().data.end());
        si.ctime       = result.ok().stat.ctime;
        si.mtime       = result.ok().stat.mtime;
        si.version     = result.ok().stat.version;
        si.data_length = result.ok().stat.data_length;
    } else {
        std::cerr << "[registry] get_service(" << node_path
                  << ") error: " << result.err() << std::endl;
    }
    return si;
}

bool ServiceRegistry::node_exists(const std::string& path) {
    if (!connected_) return false;

    auto future = client_->exists(path);

    using namespace std::chrono_literals;
    while (future.wait_for(10ms) == std::future_status::timeout) {
        ios_.poll_one();
    }

    auto result = future.get();
    return result.is_ok();
}

bool ServiceRegistry::register_service(const std::string& services_path,
                                        const std::string& node_name,
                                        const std::string& address) {
    if (!connected_) return false;

    std::string full_path = services_path + "/" + node_name;
    std::vector<char> data(address.begin(), address.end());

    auto future = client_->create(
        full_path, std::move(data),
        zk_client::acl_open_unsafe(),
        1); // flags=1 → ephemeral

    using namespace std::chrono_literals;
    while (future.wait_for(10ms) == std::future_status::timeout) {
        ios_.poll_one();
    }

    auto result = future.get();
    if (result.is_err()) {
        std::cerr << "[registry] register(" << full_path
                  << ") error: " << result.err() << std::endl;
        return false;
    }
    std::cout << "[registry] registered: " << full_path
              << " → " << address << std::endl;
    return true;
}

bool ServiceRegistry::unregister_service(const std::string& node_path) {
    if (!connected_) return false;

    auto future = client_->remove(node_path);

    using namespace std::chrono_literals;
    while (future.wait_for(10ms) == std::future_status::timeout) {
        ios_.poll_one();
    }

    auto result = future.get();
    if (result.is_err()) {
        std::cerr << "[registry] unregister(" << node_path
                  << ") error: " << result.err() << std::endl;
        return false;
    }
    return true;
}

bool ServiceRegistry::create_persistent_node(const std::string& path) {
    if (!connected_) return false;

    auto future = client_->create(
        path, {},
        zk_client::acl_open_unsafe(),
        0); // flags=0 → persistent

    using namespace std::chrono_literals;
    while (future.wait_for(10ms) == std::future_status::timeout) {
        ios_.poll_one();
    }

    auto result = future.get();
    // node already exists is not an error
    if (result.is_err() && result.err().find("node exists") == std::string::npos) {
        std::cerr << "[registry] create_node(" << path
                  << ") error: " << result.err() << std::endl;
        return false;
    }
    return true;
}

} // namespace rpc
