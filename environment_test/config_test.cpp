#include "load_config/load_config.h"
#include <iostream>

int main(int argc, char* argv[]) {
    // 1. 默认路径；接受命令行参数覆盖
    std::string path = (argc > 1) ? argv[1] : "src/config/server_config.json";

    // 2. 加载配置
    auto cfg = rpc::load_config(path);

    // 3. 打印配置面板
    rpc::print_config(cfg);

    // 4. 单独验证各项参数
    std::cout << "\n--- Parameter Access Test ---\n";
    std::cout << "server.host             = " << cfg.server.host << '\n';
    std::cout << "server.port             = " << cfg.server.port << '\n';
    std::cout << "server.thread_pool_size = " << cfg.server.thread_pool_size << '\n';
    std::cout << "zk.host                 = " << cfg.zk.host << '\n';
    std::cout << "zk.session_timeout_ms   = " << cfg.zk.session_timeout_ms << '\n';
    std::cout << "log.level               = " << cfg.log.level << '\n';
    std::cout << "serializer.type         = " << cfg.serializer.type << '\n';
    std::cout << "compress.enable         = " << (cfg.compress.enable ? "true" : "false") << '\n';
    std::cout << "balancer.strategy       = " << cfg.balancer.strategy << '\n';
    std::cout << "--- All parameters accessible ---\n";

    return 0;
}
