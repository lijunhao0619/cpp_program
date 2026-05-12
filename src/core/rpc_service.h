#pragma once

#include <memory>
#include <string>
#include <functional>
#include <atomic>
#include <vector>

#include "network/message_cycle.h"
#include "service/service_manager.h"
#include "core/error_code.h"
#include "protocol/rpc_protocol.h"

namespace rpc {

class ThreadPool;

// ── RPC 服务器 ──
// 组合 MessageCycle + ServiceManager + ThreadPool，提供完整的 RPC 服务端能力
class RpcServer {
public:
    RpcServer() = default;
    ~RpcServer();

    // 禁止拷贝
    RpcServer(const RpcServer&) = delete;
    RpcServer& operator=(const RpcServer&) = delete;

    // 设置线程池（用于并发处理请求）
    // 不设置则默认在 io_context 线程中同步处理
    void set_thread_pool(ThreadPool* pool);

    // 注册服务
    void register_service(std::shared_ptr<Service> service);

    // 启动服务器
    // port: 监听端口
    void start(uint16_t port);

    // 停止服务器
    void stop();

    // 是否正在运行
    bool is_running() const;

    // 获取统计信息
    size_t connection_count();

    // 获取 MessageCycle（用于扩展）
    MessageCycle& cycle() { return cycle_; }

private:
    void on_message(const RpcHeader& header, std::string body,
                    std::shared_ptr<Connection> conn);

    RpcResponse process_request(const RpcHeader& header,
                                const std::string& body);

    void send_response(const RpcHeader& req_header,
                       const RpcResponse& resp,
                       std::shared_ptr<Connection> conn);

    MessageCycle cycle_;
    ThreadPool* thread_pool_ = nullptr;
    std::atomic<bool> running_{false};
};

} // namespace rpc
