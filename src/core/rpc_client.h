#pragma once

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <memory>
#include <string>
#include <future>
#include <functional>
#include <mutex>
#include <deque>

#include "core/error_code.h"
#include "protocol/rpc_protocol.h"

namespace rpc {

// ── RPC 客户端 ──
// 连接服务器，发送 RPC 请求，匹配响应
// 线程安全：多个线程可同时调用 call()
class RpcClient {
public:
    RpcClient();
    ~RpcClient();

    // 禁止拷贝
    RpcClient(const RpcClient&) = delete;
    RpcClient& operator=(const RpcClient&) = delete;

    // 连接远程 RPC 服务器
    // 返回 future，可通过 future.get() 等待连接完成
    std::future<void> connect(const std::string& host, uint16_t port);

    // 发起 RPC 调用（线程安全）
    // service: 服务名
    // method:  方法名
    // params:  序列化后的参数
    // 返回 future<RpcResponse>
    std::future<RpcResponse> call(const std::string& service,
                                  const std::string& method,
                                  const std::string& params,
                                  uint32_t timeout_ms = 5000);

    // 断开连接
    void disconnect();

    // 是否已连接
    bool is_connected() const;

    // 获取内部 io_context（用于外部集成）
    boost::asio::io_context& io_context() { return ios_; }

private:
    // 持续读取响应循环
    void start_read_loop();
    void do_read_header();
    void do_read_body(const RpcHeader& header);

    // 写队列
    struct WriteItem {
        std::string frame;
        uint32_t seq_id;
    };
    void enqueue_write(std::shared_ptr<WriteItem> item);
    void do_write();

    void reject_all_pending(const std::string& reason);

    boost::asio::io_context ios_;
    boost::asio::ip::tcp::socket socket_;
    boost::asio::executor_work_guard<
        boost::asio::io_context::executor_type> work_guard_;
    std::atomic<uint32_t> next_seq_id_{1};
    std::atomic<bool> connected_{false};

    // 待匹配的请求
    struct PendingRequest {
        std::promise<RpcResponse> promise;
    };
    std::unordered_map<uint32_t, std::shared_ptr<PendingRequest>> pending_;
    std::mutex pending_mux_;

    // 写队列（序列化并发写入）
    std::deque<std::shared_ptr<WriteItem>> write_queue_;
    std::mutex write_mux_;
    bool writing_ = false;

    // 后台 IO 线程
    std::thread io_thread_;
};

} // namespace rpc
