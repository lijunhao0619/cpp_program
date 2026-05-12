#include "connection.h"

#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>

namespace rpc {

Connection::Connection(boost::asio::ip::tcp::socket socket)
    : socket_(std::move(socket)) {}

Connection::~Connection() {
    close();
}

bool Connection::is_open() const {
    return socket_.is_open() && !closed_.load();
}

void Connection::start_read(MessageHandler on_message, CloseHandler on_close) {
    on_message_ = std::move(on_message);
    on_close_ = std::move(on_close);
    do_read_header();
}

void Connection::do_read_header() {
    auto self = shared_from_this();
    boost::asio::async_read(
        socket_,
        boost::asio::buffer(header_buf_.data(), 20),
        [self](boost::system::error_code ec, size_t /*len*/) {
            if (ec) {
                bool expected = false;
                if (self->closed_.compare_exchange_strong(expected, true)) {
                    if (self->on_close_) self->on_close_(self);
                }
                return;
            }

            RpcHeader header;
            try {
                std::string hdr_data(self->header_buf_.data(), 20);
                header = deserialize_header(hdr_data);
            } catch (...) {
                bool expected = false;
                if (self->closed_.compare_exchange_strong(expected, true)) {
                    if (self->on_close_) self->on_close_(self);
                }
                return;
            }

            if (header.magic != RpcHeader::MAGIC) {
                bool expected = false;
                if (self->closed_.compare_exchange_strong(expected, true)) {
                    if (self->on_close_) self->on_close_(self);
                }
                return;
            }

            self->do_read_body(header);
        });
}

void Connection::do_read_body(RpcHeader header) {
    auto self = shared_from_this();
    body_buf_.resize(header.body_size);

    boost::asio::async_read(
        socket_,
        boost::asio::buffer(body_buf_.data(), header.body_size),
        [self, header](boost::system::error_code ec, size_t /*len*/) {
            if (ec) {
                bool expected = false;
                if (self->closed_.compare_exchange_strong(expected, true)) {
                    if (self->on_close_) self->on_close_(self);
                }
                return;
            }

            std::string body(self->body_buf_.data(), self->body_buf_.size());

            if (self->on_message_) {
                self->on_message_(header, std::move(body), self);
            }

            // 继续读取下一条消息
            self->do_read_header();
        });
}

void Connection::send(const RpcHeader& header, const std::string& body) {
    if (closed_.load()) return;

    auto data = std::make_shared<std::string>();
    *data = serialize_header(header);
    *data += body;

    {
        std::lock_guard<std::mutex> lk(write_mux_);
        write_queue_.push_back({data});
        if (writing_) return;
        writing_ = true;
    }

    do_write();
}

void Connection::do_write() {
    std::shared_ptr<std::string> data;
    {
        std::lock_guard<std::mutex> lk(write_mux_);
        if (write_queue_.empty()) {
            writing_ = false;
            return;
        }
        data = write_queue_.front().data;
        write_queue_.erase(write_queue_.begin());
    }

    auto self = shared_from_this();
    boost::asio::async_write(
        socket_,
        boost::asio::buffer(data->data(), data->size()),
        [self, data](boost::system::error_code ec, size_t /*len*/) {
            if (ec) {
                bool expected = false;
                if (self->closed_.compare_exchange_strong(expected, true)) {
                    if (self->on_close_) self->on_close_(self);
                }
                return;
            }
            self->do_write();
        });
}

void Connection::close() {
    bool expected = false;
    if (!closed_.compare_exchange_strong(expected, true)) {
        return;  // 已经关闭
    }
    if (socket_.is_open()) {
        boost::system::error_code ec;
        socket_.close(ec);
    }
}

} // namespace rpc
