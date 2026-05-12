#pragma once

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <functional>
#include <memory>
#include <string>
#include <array>
#include <vector>
#include <mutex>
#include <atomic>

#include "protocol/rpc_protocol.h"

namespace rpc {

// ── TCP 连接 ──
// 每个 Connection 管理一条 TCP 连接的生命周期，
// 负责异步读取 RPC 协议帧并回调上层。
class Connection : public std::enable_shared_from_this<Connection> {
public:
    // 消息回调: (帧头, body数据, 本连接)
    using MessageHandler = std::function<void(
        const RpcHeader&, std::string body,
        std::shared_ptr<Connection>)>;

    // 关闭回调
    using CloseHandler = std::function<void(std::shared_ptr<Connection>)>;

    explicit Connection(boost::asio::ip::tcp::socket socket);

    ~Connection();

    // 获取底层 socket
    boost::asio::ip::tcp::socket& socket() { return socket_; }

    // 连接是否打开
    bool is_open() const;

    // 开始读取消息循环（每个连接调用一次）
    void start_read(MessageHandler on_message, CloseHandler on_close = nullptr);

    // 发送响应（帧头 + body）
    void send(const RpcHeader& header, const std::string& body);

    // 主动关闭
    void close();

    // 连接唯一 ID（由 ConnectionManager 分配）
    uint64_t conn_id() const { return conn_id_; }
    void set_conn_id(uint64_t id) { conn_id_ = id; }

private:
    void do_read_header();
    void do_read_body(RpcHeader header);
    void do_write();

    boost::asio::ip::tcp::socket socket_;
    uint64_t conn_id_ = 0;

    // 读取缓冲区
    std::array<char, 20> header_buf_;
    std::vector<char> body_buf_;

    // 写队列（防止并发写）
    struct WriteEntry {
        std::shared_ptr<std::string> data;
    };
    std::vector<WriteEntry> write_queue_;
    bool writing_ = false;
    std::mutex write_mux_;

    // 回调
    MessageHandler on_message_;
    CloseHandler on_close_;
    std::atomic<bool> closed_{false};
};

} // namespace rpc
