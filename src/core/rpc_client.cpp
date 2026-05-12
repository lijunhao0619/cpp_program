#include "rpc_client.h"

#include <boost/asio/connect.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>
#include <stdexcept>

namespace rpc {

RpcClient::RpcClient()
    : ios_()
    , socket_(ios_)
    , work_guard_(boost::asio::make_work_guard(ios_)) {}

RpcClient::~RpcClient() {
    disconnect();
}

std::future<void> RpcClient::connect(const std::string& host, uint16_t port) {
    auto promise = std::make_shared<std::promise<void>>();
    auto future = promise->get_future();

    auto resolver = std::make_shared<boost::asio::ip::tcp::resolver>(ios_);
    resolver->async_resolve(
        host, std::to_string(port),
        [this, promise, resolver](
            boost::system::error_code ec,
            boost::asio::ip::tcp::resolver::results_type eps) {
            if (ec) {
                promise->set_exception(
                    std::make_exception_ptr(std::runtime_error(ec.message())));
                return;
            }

            auto self = this;
            boost::asio::async_connect(
                socket_, eps,
                [self, promise](
                    boost::system::error_code ec,
                    boost::asio::ip::tcp::endpoint) {
                    if (ec) {
                        promise->set_exception(
                            std::make_exception_ptr(
                                std::runtime_error(ec.message())));
                        return;
                    }
                    self->connected_ = true;
                    self->start_read_loop();
                    promise->set_value();
                });
        });

    // 在后台线程运行 io_context
    io_thread_ = std::thread([this] { ios_.run(); });

    return future;
}

std::future<RpcResponse> RpcClient::call(
    const std::string& service,
    const std::string& method,
    const std::string& params,
    uint32_t timeout_ms) {

    auto pending = std::make_shared<PendingRequest>();
    auto future = pending->promise.get_future();

    uint32_t seq_id = next_seq_id_++;
    {
        std::lock_guard<std::mutex> lk(pending_mux_);
        pending_[seq_id] = pending;
    }

    // 构造请求体
    RpcRequest req;
    req.service_name = service;
    req.method_name  = method;
    req.timeout_ms   = timeout_ms;
    req.params       = params;

    std::string req_body = req.serialize();

    // 构造帧头
    RpcHeader header{};
    header.magic      = RpcHeader::MAGIC;
    header.version    = RpcHeader::VERSION;
    header.msg_type   = static_cast<uint8_t>(MsgType::REQUEST);
    header.serializer = static_cast<uint8_t>(SerType::PROTOBUF);
    header.flags      = FLAG_NONE;
    header.seq_id     = seq_id;
    header.body_size  = static_cast<uint32_t>(req_body.size());
    header.checksum   = 0;

    std::string frame = serialize_header(header);
    frame += req_body;

    auto item = std::make_shared<WriteItem>();
    item->frame = std::move(frame);
    item->seq_id = seq_id;
    enqueue_write(std::move(item));

    return future;
}

// ── 持续读取循环 ──
void RpcClient::start_read_loop() {
    do_read_header();
}

void RpcClient::do_read_header() {
    auto buf = std::make_shared<std::array<char, 20>>();

    boost::asio::async_read(
        socket_,
        boost::asio::buffer(buf->data(), 20),
        [this, buf](boost::system::error_code ec, size_t) {
            if (ec) {
                reject_all_pending(ec.message());
                return;
            }

            std::string hdr_data(buf->data(), 20);
            RpcHeader header;
            try {
                header = deserialize_header(hdr_data);
            } catch (...) {
                reject_all_pending("bad header");
                return;
            }

            if (header.magic != RpcHeader::MAGIC) {
                reject_all_pending("bad magic");
                return;
            }

            do_read_body(header);
        });
}

void RpcClient::do_read_body(const RpcHeader& header) {
    auto body_buf = std::make_shared<std::vector<char>>(header.body_size);

    boost::asio::async_read(
        socket_,
        boost::asio::buffer(body_buf->data(), header.body_size),
        [this, header, body_buf](boost::system::error_code ec, size_t) {
            if (ec) {
                reject_all_pending(ec.message());
                return;
            }

            std::string body(body_buf->data(), body_buf->size());

            RpcResponse resp;
            try {
                resp = RpcResponse::deserialize(body);
            } catch (...) {
                reject_all_pending("bad response");
                return;
            }

            // 匹配请求
            std::shared_ptr<PendingRequest> pending;
            {
                std::lock_guard<std::mutex> lk(pending_mux_);
                auto it = pending_.find(header.seq_id);
                if (it != pending_.end()) {
                    pending = it->second;
                    pending_.erase(it);
                }
            }

            if (pending) {
                pending->promise.set_value(std::move(resp));
            }

            // 继续读取下一个响应
            do_read_header();
        });
}

// ── 写队列 ──
void RpcClient::enqueue_write(std::shared_ptr<WriteItem> item) {
    bool start_write = false;
    {
        std::lock_guard<std::mutex> lk(write_mux_);
        write_queue_.push_back(std::move(item));
        if (!writing_) {
            writing_ = true;
            start_write = true;
        }
    }
    if (start_write) {
        do_write();
    }
}

void RpcClient::do_write() {
    std::shared_ptr<WriteItem> item;
    {
        std::lock_guard<std::mutex> lk(write_mux_);
        if (write_queue_.empty()) {
            writing_ = false;
            return;
        }
        item = write_queue_.front();
        write_queue_.pop_front();
    }

    boost::asio::async_write(
        socket_,
        boost::asio::buffer(item->frame.data(), item->frame.size()),
        [this, item](boost::system::error_code ec, size_t) {
            if (ec) {
                // 写入失败，回退该请求的 pending
                std::lock_guard<std::mutex> lk(pending_mux_);
                auto it = pending_.find(item->seq_id);
                if (it != pending_.end()) {
                    RpcResponse err_resp;
                    err_resp.error_code = static_cast<int32_t>(ErrorCode::CONNECTION_CLOSED);
                    err_resp.error_msg  = ec.message();
                    it->second->promise.set_value(err_resp);
                    pending_.erase(it);
                }
            }
            // 继续处理队列中的下一个写入
            do_write();
        });
}

void RpcClient::reject_all_pending(const std::string& reason) {
    std::lock_guard<std::mutex> lk(pending_mux_);
    RpcResponse err_resp;
    err_resp.error_code = static_cast<int32_t>(ErrorCode::CONNECTION_CLOSED);
    err_resp.error_msg  = reason;
    for (auto& [id, pending] : pending_) {
        pending->promise.set_value(err_resp);
    }
    pending_.clear();
}

void RpcClient::disconnect() {
    connected_ = false;
    if (socket_.is_open()) {
        boost::system::error_code ec;
        socket_.close(ec);
    }
    work_guard_.reset();
    ios_.stop();

    if (io_thread_.joinable()) {
        io_thread_.join();
    }
}

bool RpcClient::is_connected() const {
    return connected_.load();
}

} // namespace rpc
