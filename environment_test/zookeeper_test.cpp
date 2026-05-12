// ============================================================
// ZooKeeper C 客户端常用操作教程
// 依赖: Apache ZooKeeper C client (libzookeeper)
// API 文档: https://zookeeper.apache.org/doc/current/api/zookeeper_8h.html
// ============================================================
//
// 编译 (MSYS2/MinGW UCRT64):
//   g++ -DTHREADED -mno-xop -D_X86INTRIN_H_INCLUDED -D_EMMINTRIN_H_INCLUDED
//       -I/e/msys/ucrt64/include zookeeper_test.cpp
//       -L/e/msys/ucrt64/lib -lzookeeper -lhashtable
//       -lws2_32 -lkernel32 -luser32 -lgdi32 -lwinspool -lshell32
//       -lole32 -loleaut32 -luuid -lcomdlg32 -ladvapi32
//       -o zookeeper_test.exe
// ============================================================

#include <zookeeper/zookeeper.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <chrono>

// ╔══════════════════════════════════════════════════════╗
// ║  全局变量 & 回调                                    ║
// ╚══════════════════════════════════════════════════════╝

static std::string connected_host;
static bool is_connected = false;

// ╔══════════════════════════════════════════════════════╗
// ║  1. 连接 ZooKeeper 服务器                           ║
// ╚══════════════════════════════════════════════════════╝

// 1a. 连接 watcher 回调 —— 监听连接状态变化
void connection_watcher(zhandle_t* zh, int type, int state,
                         const char* path, void* watcherCtx) {
    if (type == ZOO_SESSION_EVENT) {
        if (state == ZOO_CONNECTED_STATE) {
            is_connected = true;
            printf("[ZK] Connected! Session: 0x%llx\n",
                   zoo_client_id(zh)->client_id);
        } else if (state == ZOO_EXPIRED_SESSION_STATE) {
            is_connected = false;
            printf("[ZK] Session expired, reconnect needed\n");
        } else if (state == ZOO_CONNECTING_STATE) {
            printf("[ZK] Connecting...\n");
        }
    }
}

zhandle_t* zk_connect(const std::string& host) {
    zhandle_t* zh = zookeeper_init(host.c_str(),
        connection_watcher, 10000, nullptr, nullptr, 0);
    if (!zh) {
        printf("[ZK] zoo_init failed\n");
        return nullptr;
    }

    // 等待连接建立
    int waited = 0;
    while (!is_connected && waited < 100) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        waited++;
    }
    if (is_connected) {
        printf("[ZK] Connected to %s\n", host.c_str());
    }
    return zh;
}

// ╔══════════════════════════════════════════════════════╗
// ║  2. 创建节点 (服务注册核心)                          ║
// ╚══════════════════════════════════════════════════════╝

void zk_create_node(zhandle_t* zh) {
    // 2a. 创建持久节点 —— 重启后依然存在
    int ret = zoo_create(zh,
        "/services",                    // 路径
        nullptr,                        // 数据
        0,                              // 数据长度
        &ZOO_OPEN_ACL_UNSAFE,          // ACL (开放权限)
        0,                              // 0 = 持久节点
        nullptr,                        // 实际路径(输出)
        0);                             // 路径缓冲区大小
    printf("create /services: %s (code=%d)\n",
           (ret == ZOK) ? "OK" : "FAIL", ret);

    // 2b. 创建临时节点 —— 会话断开自动删除 (服务注册用这个!)
    std::string service_path = "/services/MathService";
    std::string service_data = "192.168.1.100:8080";  // 服务地址

    ret = zoo_create(zh,
        service_path.c_str(),
        service_data.c_str(),
        service_data.size(),
        &ZOO_OPEN_ACL_UNSAFE,
        ZOO_EPHEMERAL,                 // ★ 临时节点! 心跳断开就消失
        nullptr, 0);
    printf("create %s (EPHEMERAL): %s (code=%d)\n",
           service_path.c_str(),
           (ret == ZOK) ? "OK" : "FAIL", ret);

    // 2c. 创建顺序节点 —— 自动加递增后缀
    ret = zoo_create(zh,
        "/services/instance_",         // 前缀
        "192.168.1.100:8080",
        strlen("192.168.1.100:8080"),
        &ZOO_OPEN_ACL_UNSAFE,
        ZOO_EPHEMERAL | ZOO_SEQUENCE, // 临时 + 顺序 = 自动编号
        nullptr, 0);
    printf("create sequential: %s (code=%d)\n",
           (ret == ZOK) ? "OK" : "FAIL", ret);
}

// ╔══════════════════════════════════════════════════════╗
// ║  3. 读取节点数据 (服务发现核心)                      ║
// ╚══════════════════════════════════════════════════════╝

void zk_get_node(zhandle_t* zh) {
    char buffer[1024];
    int buffer_len = sizeof(buffer);
    struct Stat stat;

    // 3a. zoo_get —— 读取节点数据和元信息
    int ret = zoo_get(zh,
        "/services/MathService",
        0,                              // 0 = 不设置 watch
        buffer, &buffer_len, &stat);

    if (ret == ZOK) {
        std::string data(buffer, buffer_len);
        printf("GET /services/MathService:\n");
        printf("  data: %s\n", data.c_str());
        printf("  version: %d\n", stat.version);
        printf("  ephemeral: %lld\n", stat.ephemeralOwner);
    } else {
        printf("GET failed: code=%d (%s)\n", ret, zerror(ret));
    }
}

// ╔══════════════════════════════════════════════════════╗
// ║  4. 列出子节点 (获取服务列表)                        ║
// ╚══════════════════════════════════════════════════════╝

void zk_list_children(zhandle_t* zh) {
    struct String_vector children;
    int ret = zoo_get_children(zh,
        "/services",
        0,                              // 0 = 不设置 watch
        &children);

    if (ret == ZOK) {
        printf("Children of /services (%d):\n", children.count);
        for (int i = 0; i < children.count; i++) {
            printf("  - %s\n", children.data[i]);
        }
        // 释放内存 (ZK 分配的)
        deallocate_String_vector(&children);
    }
}

// ╔══════════════════════════════════════════════════════╗
// ║  5. Watch 机制 (监听变化 —— 服务发现的动态更新)      ║
// ╚══════════════════════════════════════════════════════╝

// 5a. 获取子节点 + 设置 watch
void zk_watch_children(zhandle_t* zh);

// 子节点变化回调
void child_watcher(zhandle_t* zh, int type, int state,
                    const char* path, void* ctx) {
    if (type == ZOO_CHILD_EVENT) {
        printf("[WATCH] Children of %s changed!\n", path);

        struct String_vector children;
        zoo_get_children(zh, path, 0, &children);
        printf("[WATCH] Now %d children:\n", children.count);
        for (int i = 0; i < children.count; i++) {
            printf("  - %s\n", children.data[i]);
        }
        deallocate_String_vector(&children);

        //重新设置 watch ZK watch 是一次性的，触发后需要重新设置
        zk_watch_children(zh);
    }
}

void zk_watch_children(zhandle_t* zh) {
    struct String_vector children;
    zoo_wget_children(zh,                //wget_children 带 watch
        "/services",
        child_watcher,                   // 变化时回调
        nullptr,                         // 回调上下文
        &children);

    printf("[WATCH] Watching /services (current: %d children)\n",
           children.count);
    deallocate_String_vector(&children);
}

// 5b. 数据变化 watcher
void data_watcher(zhandle_t* zh, int type, int state,
                   const char* path, void* ctx) {
    if (type == ZOO_CHANGED_EVENT) {
        printf("[WATCH] Data changed at %s!\n", path);
        char buffer[1024];
        int len = sizeof(buffer);
        struct Stat stat;
        zoo_wget(zh, path, data_watcher, nullptr, buffer, &len, &stat);
        printf("[WATCH] New data: %.*s\n", len, buffer);
    } else if (type == ZOO_DELETED_EVENT) {
        printf("[WATCH] Node deleted: %s\n", path);
    }
}

void zk_watch_data(zhandle_t* zh, const char* path) {
    char buffer[1024];
    int len = sizeof(buffer);
    struct Stat stat;
    zoo_wget(zh, path, data_watcher, nullptr, buffer, &len, &stat);
    printf("[WATCH] Watching data at %s\n", path);
}

// ╔══════════════════════════════════════════════════════╗
// ║  6. 更新节点数据                                    ║
// ╚══════════════════════════════════════════════════════╝

void zk_set_data(zhandle_t* zh) {
    // 必须传入当前 version，防止并发冲突 (乐观锁)
    struct Stat stat;
    char buf[1024]; int len = sizeof(buf);
    zoo_get(zh, "/services/MathService", 0, buf, &len, &stat);

    std::string new_data = "192.168.1.100:9090";  // 端口变了
    int ret = zoo_set(zh,
        "/services/MathService",
        new_data.c_str(),
        new_data.size(),
        stat.version);                 // ★ 乐观锁: CAS version

    if (ret == ZOK) {
        printf("SET /services/MathService OK\n");
    } else if (ret == ZBADVERSION) {
        printf("SET failed: version conflict (someone else updated it)\n");
    }
}

// ╔══════════════════════════════════════════════════════╗
// ║  7. 删除节点                                        ║
// ╚══════════════════════════════════════════════════════╝

void zk_delete_node(zhandle_t* zh) {
    struct Stat stat;
    char buf[1024]; int len = sizeof(buf);
    zoo_get(zh, "/services/MathService", 0, buf, &len, &stat);

    int ret = zoo_delete(zh,
        "/services/MathService",
        stat.version);                 // 同样需要 version

    printf("DELETE /services/MathService: %s (code=%d)\n",
           (ret == ZOK) ? "OK" : "FAIL", ret);
}

// ╔══════════════════════════════════════════════════════╗
// ║  8. 节点是否存在                                    ║
// ╚══════════════════════════════════════════════════════╝

void zk_exists(zhandle_t* zh, const char* path) {
    struct Stat stat;
    int ret = zoo_exists(zh, path, 0, &stat);

    if (ret == ZOK) {
        printf("EXISTS %s: YES (ephemeral=%lld)\n",
               path, stat.ephemeralOwner);
    } else if (ret == ZNONODE) {
        printf("EXISTS %s: NO\n", path);
    }
}

// ╔══════════════════════════════════════════════════════╗
// ║  9. 错误码参考                                      ║
// ╚══════════════════════════════════════════════════════╝

void zk_error_ref() {
    printf("\n常见 ZK 错误码:\n");
    printf("  ZOK            =  0  成功\n");
    printf("  ZSYSTEMERROR   = -1  系统错误\n");
    printf("  ZNONODE        = -101 节点不存在\n");
    printf("  ZNOAUTH        = -102 无权限\n");
    printf("  ZBADVERSION    = -103 版本冲突(CAS失败)\n");
    printf("  ZNODEEXISTS    = -110 节点已存在\n");
    printf("  ZNOTEMPTY      = -111 节点非空(不能删)\n");
    printf("  ZSESSIONEXPIRED= -112 session 过期\n");
    printf("  ZCONNECTIONLOSS= -4   连接断开(可重试)\n");
}

// ╔══════════════════════════════════════════════════════╗
// ║  10. RPC 服务注册 & 发现完整流程                     ║
// ╚══════════════════════════════════════════════════════╝

void zk_rpc_full_example(zhandle_t* zh) {
    printf("\n=== RPC 服务注册 & 发现 完整流程 ===\n");

    // ---- 服务提供者 ----
    // 1. 确保父节点存在
    zoo_create(zh, "/rpc_services", nullptr, 0,
               &ZOO_OPEN_ACL_UNSAFE, 0, nullptr, 0);

    // 2. 注册服务（临时节点 = 下线自动清理）
    std::string service_host = "192.168.1.10:8080";
    zoo_create(zh,
        "/rpc_services/OrderService",
        service_host.c_str(), service_host.size(),
        &ZOO_OPEN_ACL_UNSAFE,
        ZOO_EPHEMERAL,
        nullptr, 0);
    printf("[Provider] OrderService registered at %s\n", service_host.c_str());

    // ---- 服务消费者 ----
    // 3. 发现服务列表
    struct String_vector svcs;
    zoo_get_children(zh, "/rpc_services", 0, &svcs);
    printf("[Consumer] Available services:\n");
    for (int i = 0; i < svcs.count; i++) {
        // 4. 获取每个服务的地址
        char addr[256]; int addr_len = sizeof(addr);
        std::string full_path = "/rpc_services/" + std::string(svcs.data[i]);
        zoo_get(zh, full_path.c_str(), 0, addr, &addr_len, nullptr);
        printf("  %s -> %.*s\n", svcs.data[i], addr_len, addr);
    }
    deallocate_String_vector(&svcs);
}

// ╔══════════════════════════════════════════════════════╗
// ║  11. 关闭连接                                       ║
// ╚══════════════════════════════════════════════════════╝

void zk_close(zhandle_t* zh) {
    // 关闭会话，临时节点自动删除
    zookeeper_close(zh);
    printf("[ZK] Connection closed\n");
}

// ╔══════════════════════════════════════════════════════╗
// ║  main —— 运行教程                                   ║
// ╚══════════════════════════════════════════════════════╝

int main() {
    printf("=== ZooKeeper C Client Tutorial ===\n\n");

    // 1. 连接
    zhandle_t* zh = zk_connect("127.0.0.1:2181");
    if (!zh) return 1;

    // 2. 创建节点
    printf("\n--- 2. Create ---\n");
    zk_create_node(zh);

    // 3. 读取节点
    printf("\n--- 3. Get ---\n");
    zk_get_node(zh);

    // 4. 列出子节点
    printf("\n--- 4. Children ---\n");
    zk_list_children(zh);

    // 5. Watch 监听
    printf("\n--- 5. Watch ---\n");
    zk_watch_children(zh);
    zk_watch_data(zh, "/services/MathService");

    // 6. 更新数据
    printf("\n--- 6. Set ---\n");
    zk_set_data(zh);

    // 7. 检查存在
    printf("\n--- 7. Exists ---\n");
    zk_exists(zh, "/services");
    zk_exists(zh, "/nonexistent");

    // 8. 删除
    printf("\n--- 8. Delete ---\n");
    zk_delete_node(zh);

    // 9. RPC 完整流程
    zk_rpc_full_example(zh);

    // 10. 错误码参考
    zk_error_ref();

    // 11. 关闭
    zk_close(zh);
    return 0;
}
