#include "message_cycle.h"

#include <boost/asio/signal_set.hpp>

namespace rpc {

MessageCycle::MessageCycle()
    : ios_()
    , acceptor_(ios_) {}

MessageCycle::~MessageCycle() {
    stop();
}

void MessageCycle::start_listen(uint16_t port, MessageHandler handler) {
    on_message_ = std::move(handler);

    boost::asio::ip::tcp::endpoint ep(boost::asio::ip::tcp::v4(), port);
    acceptor_.open(ep.protocol());
    acceptor_.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
    acceptor_.bind(ep);
    acceptor_.listen(256);

    do_accept();
}

void MessageCycle::run() {
    running_ = true;

    // 捕获 SIGINT/SIGTERM 以支持优雅关闭（Unix only，Windows 忽略）
#ifndef _WIN32
    boost::asio::signal_set signals(ios_, SIGINT, SIGTERM);
    signals.async_wait([this](boost::system::error_code, int) {
        stop();
    });
#endif

    ios_.run();
}

void MessageCycle::run_async() {
    worker_ = std::thread([this] { run(); });
}

void MessageCycle::stop() {
    running_ = false;

    if (acceptor_.is_open()) {
        boost::system::error_code ec;
        acceptor_.close(ec);
    }

    conn_mgr_.close_all();
    ios_.stop();

    if (worker_.joinable()) {
        worker_.join();
    }
}

void MessageCycle::do_accept() {
    acceptor_.async_accept(
        [this](boost::system::error_code ec,
               boost::asio::ip::tcp::socket socket) {
            if (ec) {
                if (running_) do_accept();
                return;
            }

            // 创建 Connection 并加入管理器
            auto conn = std::make_shared<Connection>(std::move(socket));
            uint64_t id = conn_mgr_.add(conn);
            (void)id;

            // 设置关闭回调：连接断开时自动从管理器移除
            auto close_cb = [this](std::shared_ptr<Connection> c) {
                conn_mgr_.remove(c->conn_id());
            };

            // 开始读取该连接的消息
            conn->start_read(on_message_, close_cb);

            // 继续接受下一个连接
            if (running_) do_accept();
        });
}

} // namespace rpc
