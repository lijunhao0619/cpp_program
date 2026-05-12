#pragma once

#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>
#include <boost/endian/conversion.hpp>
#include <array>
#include <atomic>
#include <cstring>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "zk_proto.hpp"

namespace zk_client {

template <typename T>
class result {
public:
    static result make_ok(T&& val) {
        result r;
        r.ok_ = std::move(val);
        return r;
    }

    static result make_error(std::string msg) {
        result r;
        r.err_ = std::move(msg);
        return r;
    }

    bool is_ok() const { return ok_.has_value(); }
    bool is_err() const { return !is_ok(); }

    T& ok() { return *ok_; }
    const T& ok() const { return *ok_; }
    const std::string& err() const { return err_; }

private:
    std::optional<T> ok_;
    std::string err_;
};

template <>
class result<void> {
public:
    static result make_ok() { return {}; }
    static result make_error(std::string msg) {
        result r;
        r.err_ = std::move(msg);
        return r;
    }
    bool is_ok() const { return !has_err_; }
    bool is_err() const { return has_err_; }
    const std::string& err() const { return err_; }
private:
    std::string err_;
    bool has_err_ = false;
};

class zk_client : public std::enable_shared_from_this<zk_client> {
    boost::asio::io_context& ios_;
    boost::asio::ip::tcp::socket sock_;
    boost::asio::ip::tcp::resolver resolver_;

    struct pending_entry {
        void* promise_ptr;
        void (*resolve)(void* promise, int32_t err, const char* data);
        void (*reject)(void* promise, std::string err);
        std::shared_ptr<void> keeper; // keeps promise alive
    };
    std::unordered_map<int32_t, pending_entry> pending_;
    std::mutex pending_mux_;

    std::atomic<int32_t> xid_{1};
    int64_t session_id_ = 0;
    std::vector<char> passwd_ = std::vector<char>(16, '\0');
    int32_t timeout_ms_ = 4000;
    bool connected_ = false;

public:
    explicit zk_client(boost::asio::io_context& ios)
        : ios_(ios), sock_(ios), resolver_(ios) {}

    ~zk_client() {
        if (sock_.is_open()) {
            boost::system::error_code ec;
            sock_.close(ec);
        }
    }

    // ---- connect --------------------------------------------------
    std::future<result<void>> connect(std::string host, uint16_t port,
                                      int32_t timeout_ms = 4000) {
        timeout_ms_ = timeout_ms;
        auto promise = std::make_shared<std::promise<result<void>>>();
        auto future = promise->get_future();

        boost::system::error_code resolve_ec;
        auto eps = resolver_.resolve(host, std::to_string(port), resolve_ec);
        if (resolve_ec) {
            promise->set_value(result<void>::make_error(resolve_ec.message()));
            return future;
        }

        boost::asio::async_connect(
            sock_, eps,
            [self = shared_from_this(), promise](
                boost::system::error_code ec,
                boost::asio::ip::tcp::endpoint) {
                if (ec) {
                    promise->set_value(
                        result<void>::make_error(ec.message()));
                    return;
                }
                self->do_handshake(promise);
            });

        return future;
    }

    // ---- CRUD operations ------------------------------------------

    auto create(std::string path, std::vector<char> data,
                std::vector<zk_acl> acls, int32_t flags) {
        create_request req{std::move(path), std::move(data), std::move(acls), flags};
        return send_request<create_response>(op_code::create,
                                              req.serialize_body());
    }

    auto get_children(std::string path, bool watch = false) {
        get_children_request req{std::move(path), watch};
        return send_request<get_children_response>(op_code::get_children,
                                                    req.serialize_body());
    }

    auto get_data(std::string path, bool watch = false) {
        get_data_request req{std::move(path), watch};
        return send_request<get_data_response>(op_code::get_data,
                                                req.serialize_body());
    }

    auto exists(std::string path, bool watch = false) {
        exists_request req{std::move(path), watch};
        return send_request<exists_response>(op_code::exists,
                                              req.serialize_body());
    }

    auto remove(std::string path, int32_t version = -1) {
        delete_request req{std::move(path), version};
        return send_request_void(op_code::remove, req.serialize_body());
    }

private:
    // ---- handshake ------------------------------------------------
    void do_handshake(std::shared_ptr<std::promise<result<void>>> promise) {
        connect_request cr;
        cr.timeout_ms = timeout_ms_;
        cr.session_id = session_id_;
        cr.passwd = passwd_;
        auto body = cr.serialize();
        auto payload = make_zpayload(body);
        auto sp = std::make_shared<std::vector<char>>(std::move(payload));

        boost::asio::async_write(
            sock_, boost::asio::buffer(sp->data(), sp->size()),
            [self = shared_from_this(), promise, sp](
                boost::system::error_code ec, size_t) {
                if (ec) {
                    promise->set_value(result<void>::make_error(ec.message()));
                    return;
                }
                self->read_handshake_response(promise);
            });
    }

    void read_handshake_response(
        std::shared_ptr<std::promise<result<void>>> promise) {
        auto lenbuf = std::make_shared<std::array<char, 4>>();

        boost::asio::async_read(
            sock_, boost::asio::buffer(lenbuf->data(), 4),
            [self = shared_from_this(), promise, lenbuf](
                boost::system::error_code ec, size_t) {
                if (ec) {
                    promise->set_value(result<void>::make_error(ec.message()));
                    return;
                }

                int32_t n;
                std::memcpy(&n, lenbuf->data(), 4);
                auto payload_len = boost::endian::big_to_native(n);

                auto body = std::make_shared<std::vector<char>>(payload_len);
                boost::asio::async_read(
                    self->sock_,
                    boost::asio::buffer(body->data(), payload_len),
                    [self, promise, body](
                        boost::system::error_code ec, size_t) {
                        if (ec) {
                            promise->set_value(
                                result<void>::make_error(ec.message()));
                            return;
                        }
                        const char* p = body->data();
                        auto resp = connect_response::deserialize(p);
                        self->session_id_ = resp.session_id;
                        self->passwd_ = resp.passwd;
                        self->timeout_ms_ = resp.timeout_ms;
                        self->connected_ = true;

                        promise->set_value(result<void>::make_ok());
                    });
            });
    }

    // ---- per-operation write-then-read cycle -----------------------
    void reject_pending(int32_t xid, std::string msg) {
        pending_entry entry;
        {
            std::lock_guard<std::mutex> lk(pending_mux_);
            auto it = pending_.find(xid);
            if (it != pending_.end()) {
                entry = it->second;
                pending_.erase(it);
            } else {
                return;
            }
        }
        entry.reject(entry.promise_ptr, std::move(msg));
    }

    void resolve_pending(int32_t rxid, int32_t err, const char* data) {
        pending_entry entry;
        {
            std::lock_guard<std::mutex> lk(pending_mux_);
            auto it = pending_.find(rxid);
            if (it != pending_.end()) {
                entry = it->second;
                pending_.erase(it);
            } else {
                return;
            }
        }
        entry.resolve(entry.promise_ptr, err, data);
    }

    void do_read_response(int32_t xid) {
        auto lenbuf = std::make_shared<std::array<char, 4>>();
        boost::asio::async_read(
            sock_, boost::asio::buffer(lenbuf->data(), 4),
            [self = shared_from_this(), lenbuf, xid](
                boost::system::error_code ec, size_t) {
                if (ec) {
                    self->reject_pending(xid, ec.message());
                    return;
                }
                int32_t n;
                std::memcpy(&n, lenbuf->data(), 4);
                auto body_len = boost::endian::big_to_native(n);
                auto body = std::make_shared<std::vector<char>>(body_len);
                boost::asio::async_read(
                    self->sock_, boost::asio::buffer(body->data(), body_len),
                    [self, body, xid](
                        boost::system::error_code ec, size_t) {
                        if (ec) {
                            self->reject_pending(xid, ec.message());
                            return;
                        }
                        const char* p = body->data();
                        auto rh = reply_header::deserialize(p);
                        self->resolve_pending(rh.xid, rh.err, p);
                    });
            });
    }

    // ---- send with response body ----------------------------------
    template <typename Response>
    std::future<result<Response>> send_request(
        op_code op, const std::vector<char>& body) {
        auto promise = std::make_shared<std::promise<result<Response>>>();
        auto future = promise->get_future();

        int32_t xid = xid_++;
        {
            std::lock_guard<std::mutex> lk(pending_mux_);
            pending_[xid] = {
                promise.get(),
                [](void* p, int32_t err, const char* data) {
                    auto pr = static_cast<std::promise<result<Response>>*>(p);
                    if (err != 0) {
                        pr->set_value(result<Response>::make_error(
                            std::string(zk_error_str(err))));
                    } else {
                        pr->set_value(result<Response>::make_ok(
                            Response::deserialize(data)));
                    }
                },
                [](void* p, std::string msg) {
                    auto pr = static_cast<std::promise<result<Response>>*>(p);
                    pr->set_value(
                        result<Response>::make_error(std::move(msg)));
                },
                promise,
            };
        }

        do_send(xid, op, body);
        return future;
    }

    std::future<result<void>> send_request_void(
        op_code op, const std::vector<char>& body) {
        auto promise = std::make_shared<std::promise<result<void>>>();
        auto future = promise->get_future();

        int32_t xid = xid_++;
        {
            std::lock_guard<std::mutex> lk(pending_mux_);
            pending_[xid] = {
                promise.get(),
                [](void* p, int32_t err, const char*) {
                    auto pr = static_cast<std::promise<result<void>>*>(p);
                    if (err != 0) {
                        pr->set_value(result<void>::make_error(
                            std::string(zk_error_str(err))));
                    } else {
                        pr->set_value(result<void>::make_ok());
                    }
                },
                [](void* p, std::string msg) {
                    auto pr = static_cast<std::promise<result<void>>*>(p);
                    pr->set_value(result<void>::make_error(std::move(msg)));
                },
                promise,
            };
        }

        do_send(xid, op, body);
        return future;
    }

    void do_send(int32_t xid, op_code op, const std::vector<char>& body) {
        std::vector<char> full;
        request_header hdr{xid, static_cast<int32_t>(op)};
        hdr.serialize(full);
        full.insert(full.end(), body.begin(), body.end());

        auto payload = make_zpayload(full);
        auto sp = std::make_shared<std::vector<char>>(std::move(payload));
        auto self = shared_from_this();
        boost::asio::post(ios_.get_executor(),
            [self, sp, xid]() {
                boost::asio::async_write(
                    self->sock_, boost::asio::buffer(sp->data(), sp->size()),
                    [self, sp, xid](boost::system::error_code ec, size_t) {
                        if (ec) {
                            self->reject_pending(xid, ec.message());
                            return;
                        }
                        self->do_read_response(xid);
                    });
            });
    }
};

} // namespace zk_client
