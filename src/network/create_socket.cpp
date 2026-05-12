#include "create_socket.h"

#include <boost/asio/connect.hpp>
#include <memory>
#include <stdexcept>

namespace rpc {

boost::asio::ip::tcp::acceptor create_server_acceptor(
    boost::asio::io_context& ios, uint16_t port) {

    boost::asio::ip::tcp::endpoint ep(boost::asio::ip::tcp::v4(), port);
    boost::asio::ip::tcp::acceptor acceptor(ios, ep);

    acceptor.set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
    acceptor.listen(256);

    return acceptor;
}

std::future<boost::asio::ip::tcp::socket> create_client_socket(
    boost::asio::io_context& ios,
    const std::string& host, uint16_t port) {

    auto promise = std::make_shared<std::promise<boost::asio::ip::tcp::socket>>();
    auto future = promise->get_future();

    auto resolver = std::make_shared<boost::asio::ip::tcp::resolver>(ios);
    resolver->async_resolve(
        host, std::to_string(port),
        [&ios, promise, resolver](
            boost::system::error_code ec,
            boost::asio::ip::tcp::resolver::results_type eps) {
            if (ec) {
                promise->set_exception(
                    std::make_exception_ptr(std::runtime_error(ec.message())));
                return;
            }

            auto sock = std::make_shared<boost::asio::ip::tcp::socket>(ios);
            boost::asio::async_connect(
                *sock, eps,
                [promise, sock](
                    boost::system::error_code ec,
                    boost::asio::ip::tcp::endpoint) {
                    if (ec) {
                        promise->set_exception(
                            std::make_exception_ptr(std::runtime_error(ec.message())));
                        return;
                    }
                    promise->set_value(std::move(*sock));
                });
        });

    return future;
}

} // namespace rpc
