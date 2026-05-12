#pragma once

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <string>
#include <future>

namespace rpc {

// 创建服务端 tcp::acceptor，绑定指定端口并开始监听
// backlog 默认 256
boost::asio::ip::tcp::acceptor create_server_acceptor(
    boost::asio::io_context& ios,
    uint16_t port);

// 异步连接远程服务器，返回已连接的 tcp::socket
std::future<boost::asio::ip::tcp::socket> create_client_socket(
    boost::asio::io_context& ios,
    const std::string& host,
    uint16_t port);

} // namespace rpc
