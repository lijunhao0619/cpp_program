#pragma once

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <functional>
#include <thread>
#include <atomic>

#include "connection.h"
#include "connection_manager.h"

namespace rpc {

// ── 消息事件循环 ──
// 管理 io_context 生命周期，接收新连接，驱动所有异步 IO
class MessageCycle {
public:
    using MessageHandler = Connection::MessageHandler;

    MessageCycle();
    ~MessageCycle();

    // 禁止拷贝
    MessageCycle(const MessageCycle&) = delete;
    MessageCycle& operator=(const MessageCycle&) = delete;

    // 开始监听端口
    // port: 监听端口
    // handler: 收到完整消息时的回调
    void start_listen(uint16_t port, MessageHandler handler);

    // 运行事件循环（阻塞当前线程）
    void run();

    // 在后台线程中运行
    void run_async();

    // 停止事件循环
    void stop();

    // 获取 io_context
    boost::asio::io_context& io_context() { return ios_; }

    // 获取连接管理器
    ConnectionManager& connections() { return conn_mgr_; }

    // 是否正在运行
    bool is_running() const { return running_.load(); }

private:
    void do_accept();

    boost::asio::io_context ios_;
    boost::asio::ip::tcp::acceptor acceptor_;
    ConnectionManager conn_mgr_;
    MessageHandler on_message_;
    std::thread worker_;
    std::atomic<bool> running_{false};
};

} // namespace rpc
