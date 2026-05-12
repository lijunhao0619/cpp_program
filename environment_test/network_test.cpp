#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <memory>

#include "core/rpc_service.h"
#include "core/rpc_client.h"
#include "service/service_manager.h"
#include "network/connection_manager.h"
#include "network/create_socket.h"

using namespace std::chrono_literals;

static int passed = 0;
static int failed = 0;

void ok() { std::cout << "OK\n"; passed++; }
void fail(const std::string& msg) { std::cout << "FAIL: " << msg << "\n"; failed++; }

// ── 测试用 Echo 服务 ──
class EchoService : public rpc::Service {
public:
    std::string name() const override { return "EchoService"; }
    std::vector<std::string> methods() const override { return {"Echo"}; }

    std::string handle(const std::string& method,
                       const std::string& params) override {
        if (method == "Echo") return params;
        throw std::runtime_error("unknown method");
    }
};

int main() {
    std::cout << "=== Network Module Tests ===\n";

    const uint16_t TEST_PORT = 19999;

    // ── ConnectionManager tests ──
    std::cout << "  ConnectionManager add/count ... ";
    {
        boost::asio::io_context ios;
        rpc::ConnectionManager mgr;
        boost::asio::ip::tcp::socket sock(ios);
        auto conn = std::make_shared<rpc::Connection>(std::move(sock));
        uint64_t id = mgr.add(conn);
        if (mgr.count() == 1 && id == conn->conn_id()) ok(); else fail("count or id mismatch");
        mgr.close_all();  // 必须在 ios 销毁前释放连接
    }

    std::cout << "  ConnectionManager get ... ";
    {
        boost::asio::io_context ios;
        rpc::ConnectionManager mgr;
        boost::asio::ip::tcp::socket sock(ios);
        auto conn = std::make_shared<rpc::Connection>(std::move(sock));
        uint64_t id = mgr.add(conn);
        auto got = mgr.get(id);
        if (got && got->conn_id() == id) ok(); else fail("get failed");
        mgr.close_all();
    }

    std::cout << "  ConnectionManager remove ... ";
    {
        boost::asio::io_context ios;
        rpc::ConnectionManager mgr;
        boost::asio::ip::tcp::socket sock(ios);
        auto conn = std::make_shared<rpc::Connection>(std::move(sock));
        uint64_t id = mgr.add(conn);
        mgr.remove(id);
        if (mgr.count() == 0 && mgr.get(id) == nullptr) ok(); else fail("remove failed");
        mgr.close_all();
    }

    std::cout << "  ConnectionManager close_all ... ";
    {
        boost::asio::io_context ios;
        rpc::ConnectionManager mgr;
        boost::asio::ip::tcp::socket s1(ios), s2(ios);
        mgr.add(std::make_shared<rpc::Connection>(std::move(s1)));
        mgr.add(std::make_shared<rpc::Connection>(std::move(s2)));
        mgr.close_all();
        if (mgr.count() == 0) ok(); else fail("close_all failed");
    }

    // ── Error code tests ──
    std::cout << "  error_code_str OK ... ";
    {
        auto s = rpc::error_code_str(rpc::ErrorCode::OK);
        if (s == "OK") ok(); else fail("wrong string: " + s);
    }

    std::cout << "  error_code_str SERVICE_NOT_FOUND ... ";
    {
        auto s = rpc::error_code_str(rpc::ErrorCode::SERVICE_NOT_FOUND);
        if (s.find("service") != std::string::npos) ok(); else fail("wrong string: " + s);
    }

    // ── RPC Server + Client integration test ──
    std::cout << "  RPC server start/stop ... ";
    {
        rpc::RpcServer server;
        server.register_service(std::make_shared<EchoService>());

        // Run server on background thread
        std::thread server_thread([&server]() {
            server.start(TEST_PORT);
        });

        std::this_thread::sleep_for(200ms);

        if (server.is_running()) {
            server.stop();
            if (server_thread.joinable()) server_thread.join();
            ok();
        } else {
            server.stop();
            if (server_thread.joinable()) server_thread.join();
            fail("server not running");
        }
    }

    std::cout << "  client connect to server ... ";
    {
        rpc::RpcServer server;
        server.register_service(std::make_shared<EchoService>());

        std::thread server_thread([&server]() {
            server.start(TEST_PORT);
        });
        std::this_thread::sleep_for(200ms);

        rpc::RpcClient client;
        try {
            auto fut = client.connect("127.0.0.1", TEST_PORT);
            fut.get();
            if (client.is_connected()) ok(); else fail("not connected");
        } catch (const std::exception& e) {
            fail(std::string("exception: ") + e.what());
        }

        client.disconnect();
        server.stop();
        if (server_thread.joinable()) server_thread.join();
    }

    std::cout << "  RPC call success ... ";
    {
        rpc::RpcServer server;
        server.register_service(std::make_shared<EchoService>());

        std::thread server_thread([&server]() {
            server.start(TEST_PORT);
        });
        std::this_thread::sleep_for(200ms);

        rpc::RpcClient client;
        auto fut = client.connect("127.0.0.1", TEST_PORT);
        fut.get();

        auto result_fut = client.call("EchoService", "Echo", "HelloRPC");
        auto resp = result_fut.get();

        if (resp.error_code == 0 && resp.result == "HelloRPC")
            ok(); else fail("error_code=" + std::to_string(resp.error_code) + " result=" + resp.result);

        client.disconnect();
        server.stop();
        if (server_thread.joinable()) server_thread.join();
    }

    std::cout << "  RPC call service not found ... ";
    {
        rpc::RpcServer server;
        server.register_service(std::make_shared<EchoService>());

        std::thread server_thread([&server]() {
            server.start(TEST_PORT);
        });
        std::this_thread::sleep_for(200ms);

        rpc::RpcClient client;
        auto fut = client.connect("127.0.0.1", TEST_PORT);
        fut.get();

        auto result_fut = client.call("NoService", "Foo", "data");
        auto resp = result_fut.get();

        if (resp.error_code != 0 && resp.error_msg.find("service not found") != std::string::npos)
            ok(); else fail("expected service not found, got: " + resp.error_msg);

        client.disconnect();
        server.stop();
        if (server_thread.joinable()) server_thread.join();
    }

    std::cout << "  RPC call method not found ... ";
    {
        rpc::RpcServer server;
        server.register_service(std::make_shared<EchoService>());

        std::thread server_thread([&server]() {
            server.start(TEST_PORT);
        });
        std::this_thread::sleep_for(200ms);

        rpc::RpcClient client;
        auto fut = client.connect("127.0.0.1", TEST_PORT);
        fut.get();

        auto result_fut = client.call("EchoService", "NoMethod", "data");
        auto resp = result_fut.get();

        if (resp.error_code != 0 && resp.error_msg.find("method not found") != std::string::npos)
            ok(); else fail("expected method not found, got: " + resp.error_msg);

        client.disconnect();
        server.stop();
        if (server_thread.joinable()) server_thread.join();
    }

    std::cout << "  RPC call large payload ... ";
    {
        rpc::RpcServer server;
        server.register_service(std::make_shared<EchoService>());

        std::thread server_thread([&server]() {
            server.start(TEST_PORT);
        });
        std::this_thread::sleep_for(200ms);

        rpc::RpcClient client;
        auto fut = client.connect("127.0.0.1", TEST_PORT);
        fut.get();

        std::string large(100000, 'X');
        auto result_fut = client.call("EchoService", "Echo", large);
        auto resp = result_fut.get();

        if (resp.error_code == 0 && resp.result == large)
            ok(); else fail("large payload mismatch, size=" + std::to_string(resp.result.size()));

        client.disconnect();
        server.stop();
        if (server_thread.joinable()) server_thread.join();
    }

    std::cout << "  multiple sequential calls ... ";
    {
        rpc::RpcServer server;
        server.register_service(std::make_shared<EchoService>());

        std::thread server_thread([&server]() {
            server.start(TEST_PORT);
        });
        std::this_thread::sleep_for(200ms);

        rpc::RpcClient client;
        auto fut = client.connect("127.0.0.1", TEST_PORT);
        fut.get();

        bool all_ok = true;
        for (int i = 0; i < 10; i++) {
            auto resp = client.call("EchoService", "Echo", "msg" + std::to_string(i)).get();
            if (resp.error_code != 0 || resp.result != "msg" + std::to_string(i)) {
                all_ok = false;
                break;
            }
        }
        if (all_ok) ok(); else fail("sequential calls failed");

        client.disconnect();
        server.stop();
        if (server_thread.joinable()) server_thread.join();
    }

    std::cout << "  empty params call ... ";
    {
        rpc::RpcServer server;
        server.register_service(std::make_shared<EchoService>());

        std::thread server_thread([&server]() {
            server.start(TEST_PORT);
        });
        std::this_thread::sleep_for(200ms);

        rpc::RpcClient client;
        auto fut = client.connect("127.0.0.1", TEST_PORT);
        fut.get();

        auto resp = client.call("EchoService", "Echo", "").get();
        if (resp.error_code == 0 && resp.result.empty()) ok(); else fail("empty params failed");

        client.disconnect();
        server.stop();
        if (server_thread.joinable()) server_thread.join();
    }

    // ── summary ──
    std::cout << "\n=========================\n";
    std::cout << "Passed: " << passed << "\n";
    std::cout << "Failed: " << failed << "\n";

    return failed > 0 ? 1 : 0;
}
