#include "conn_balancer/conn_balancer.h"
#include "registry/node_manager.h"

#include <boost/asio/io_context.hpp>
#include <iostream>
#include <iomanip>
#include <map>

void print_sep(const char* title) {
    std::cout << "\n── " << title << " ──\n";
}

int main() {
    std::cout << "=== Load Balancer & Node Manager Test ===\n";

    // ═══════════════════════════════════════════════════════════
    // Part A: 纯负载均衡器测试（不依赖 ZK）
    // ═══════════════════════════════════════════════════════════
    print_sep("A. Load Balancer (standalone)");

    std::vector<rpc::Endpoint> nodes = {
        {"server_a", "10.0.0.1:8080", 1},
        {"server_b", "10.0.0.2:8080", 2},
        {"server_c", "10.0.0.3:8080", 3},
    };

    rpc::LoadBalancer lb;
    lb.set_nodes(nodes);

    // A1. 轮询
    std::cout << "\n[A1] Round Robin (10 picks):\n";
    lb.set_strategy(rpc::create_strategy("round_robin"));
    for (int i = 0; i < 10; ++i) {
        auto ep = lb.select();
        std::cout << "  #" << (i+1) << " → " << ep.name
                  << " (" << ep.address << ")\n";
    }

    // A2. 随机
    std::cout << "\n[A2] Random (10 picks):\n";
    lb.set_strategy(rpc::create_strategy("random"));
    std::map<std::string, int> counts;
    for (int i = 0; i < 300; ++i) {
        counts[lb.select().name]++;
    }
    for (auto& [name, cnt] : counts) {
        std::cout << "  " << name << ": " << cnt
                  << " (" << (100.0 * cnt / 300) << "%)\n";
    }

    // A3. 加权
    std::cout << "\n[A3] Weighted (300 picks, w=1,2,3):\n";
    lb.set_strategy(rpc::create_strategy("weighted"));
    counts.clear();
    for (int i = 0; i < 300; ++i) {
        counts[lb.select().name]++;
    }
    for (auto& [name, cnt] : counts) {
        std::cout << "  " << name << ": " << cnt
                  << " (" << (100.0 * cnt / 300) << "%)\n";
    }

    // A4. 动态增删
    std::cout << "\n[A4] Dynamic add/remove:\n";
    lb.add_node({"server_d", "10.0.0.4:8080", 2});
    lb.remove_node("server_a");
    std::cout << "  Nodes after add/remove: ";
    for (auto& n : lb.nodes())
        std::cout << n.name << " ";
    std::cout << "\n  Strategy: " << lb.strategy_name() << "\n";

    // ═══════════════════════════════════════════════════════════
    // Part B: NodeManager + ZK 集成测试
    // ═══════════════════════════════════════════════════════════
    print_sep("B. NodeManager + ZK");

    boost::asio::io_context ios;
    rpc::NodeManager mgr(ios);

    // B1. 连接
    std::cout << "\n[B1] Connecting to ZK...\n";
    if (!mgr.connect()) {
        std::cerr << "  FAIL: ZK not running\n";
        return 1;
    }
    std::cout << "  OK\n";

    // B2. 初始化路径
    std::cout << "\n[B2] Init ZK paths...\n";
    mgr.init_zk_paths("/rpc/services");
    std::cout << "  OK\n";

    // B3. 注册节点
    std::cout << "\n[B3] Registering 3 services...\n";
    mgr.register_node("order_svc_1", "192.168.1.10:8080", 1);
    mgr.register_node("order_svc_2", "192.168.1.11:8080", 2);
    mgr.register_node("user_svc_1",  "192.168.1.20:8080", 3);
    std::cout << "  OK\n";

    // B4. 列出 ZK 节点
    std::cout << "\n[B4] ZK children under /rpc/services:\n";
    for (auto& name : mgr.list_node_names()) {
        std::string path = "/rpc/services/" + name;
        std::cout << "  " << name << " → \""
                  << mgr.get_node_data(path) << "\"\n";
    }

    // B5. 刷新到负载均衡器
    std::cout << "\n[B5] Refresh nodes into balancer...\n";
    mgr.refresh_nodes();
    std::cout << "  " << mgr.nodes().size() << " nodes loaded\n";

    // B6. 轮询选择
    std::cout << "\n[B6] Round Robin via NodeManager:\n";
    mgr.set_balancer_strategy("round_robin");
    for (int i = 0; i < 6; ++i) {
        auto ep = mgr.select_node();
        std::cout << "  #" << (i+1) << " → " << ep.name
                  << " @ " << ep.address << "\n";
    }

    // B7. 切换随机
    std::cout << "\n[B7] Switch to Random:\n";
    mgr.set_balancer_strategy("random");
    counts.clear();
    for (int i = 0; i < 30; ++i) {
        counts[mgr.select_node().name]++;
    }
    for (auto& [name, cnt] : counts) {
        std::cout << "  " << name << ": " << cnt << " picks\n";
    }
    std::cout << "  Strategy: " << mgr.strategy_name() << "\n";

    // B8. 检查不存在的节点
    std::cout << "\n[B8] Node existence check:\n";
    std::cout << "  /rpc/services/order_svc_1 exists = "
              << (mgr.node_exists("/rpc/services/order_svc_1") ? "yes" : "no") << "\n";
    std::cout << "  /rpc/services/ghost_svc exists   = "
              << (mgr.node_exists("/rpc/services/ghost_svc") ? "yes" : "no") << "\n";

    // B9. 注销清理
    std::cout << "\n[B9] Cleanup...\n";
    mgr.unregister_node("order_svc_1");
    mgr.unregister_node("order_svc_2");
    mgr.unregister_node("user_svc_1");
    mgr.refresh_nodes();
    std::cout << "  remaining: " << mgr.nodes().size() << "\n";

    std::cout << "\n=== Test Complete ===\n";
    return 0;
}
