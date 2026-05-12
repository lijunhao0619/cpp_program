#include "registry/service_registry.h"

#include <chrono>
#include <iostream>
#include <iomanip>
#include <thread>

int main() {
    std::cout << "=== ZK Service Registry Test ===\n\n";

    boost::asio::io_context ios;
    rpc::ServiceRegistry registry(ios);

    // 1. 连接 ZK
    std::cout << "[1] Connecting to ZooKeeper...\n";
    if (!registry.connect("127.0.0.1", 2181)) {
        std::cerr << "FAIL: cannot connect to ZK. Is ZK running?\n";
        return 1;
    }
    std::cout << "    Connected OK\n\n";

    // 2. 确保持久节点存在
    std::cout << "[2] Ensuring persistent nodes...\n";
    registry.create_persistent_node("/rpc");
    registry.create_persistent_node("/rpc/services");
    std::cout << "    Done\n\n";

    // 3. 列出当前服务
    std::cout << "[3] Listing services under /rpc/services...\n";
    auto services = registry.list_services("/rpc/services");
    if (services.empty()) {
        std::cout << "    (no services registered yet)\n";
    } else {
        for (auto& svc : services) {
            std::cout << "    - " << svc.name << " → " << svc.address << "\n";
        }
    }
    std::cout << "\n";

    // 4. 注册一个测试服务
    std::cout << "[4] Registering test service...\n";
    bool ok = registry.register_service(
        "/rpc/services", "test_server_1", "192.168.1.100:8080");
    std::cout << "    " << (ok ? "OK" : "FAILED") << "\n\n";

    // 5. 再次列出
    std::cout << "[5] Listing services again...\n";
    services = registry.list_services("/rpc/services");
    for (auto& svc : services) {
        std::cout << "    name        = " << svc.name << "\n";
        std::cout << "    address     = " << svc.address << "\n";
        std::cout << "    version     = " << svc.version << "\n";
        std::cout << "    data_length = " << svc.data_length << "\n";
        std::cout << "    ---\n";
    }
    std::cout << "\n";

    // 6. 直接通过 get_children + get_node_data 读取
    std::cout << "[6] Reading via get_children + get_node_data...\n";
    auto children = registry.get_children("/rpc/services");
    for (auto& child : children) {
        std::string full = "/rpc/services/" + child;
        std::string data = registry.get_node_data(full);
        std::cout << "    " << full << " → \"" << data << "\"\n";
    }
    std::cout << "\n";

    // 7. 检查节点是否存在
    std::cout << "[7] Checking node existence...\n";
    std::cout << "    /rpc/services exists         = "
              << (registry.node_exists("/rpc/services") ? "yes" : "no") << "\n";
    std::cout << "    /rpc/nonexistent exists      = "
              << (registry.node_exists("/rpc/nonexistent") ? "yes" : "no") << "\n";
    std::cout << "\n";

    // 8. 注销测试服务
    std::cout << "[8] Unregistering test service...\n";
    ok = registry.unregister_service("/rpc/services/test_server_1");
    std::cout << "    " << (ok ? "OK" : "FAILED") << "\n\n";

    // 9. 验证已清理
    std::cout << "[9] After cleanup...\n";
    services = registry.list_services("/rpc/services");
    std::cout << "    remaining services: " << services.size() << "\n";

    std::cout << "\n=== Test Complete ===\n";
    return 0;
}
