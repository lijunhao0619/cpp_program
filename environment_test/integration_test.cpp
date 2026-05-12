#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <memory>
#include <vector>

#include "core/rpc_service.h"
#include "core/rpc_client.h"
#include "thread_pool/thread_pool.h"

using namespace std::chrono_literals;

static int passed = 0;
static int failed = 0;

void ok() { std::cout << "OK\n"; passed++; }
void fail(const std::string& msg) { std::cout << "FAIL: " << msg << "\n"; failed++; }

// ── 内置 Ping 服务 ──
class PingService : public rpc::Service {
public:
    std::string name() const override { return "PingService"; }
    std::vector<std::string> methods() const override { return {"Ping", "Stats"}; }

    std::string handle(const std::string& method,
                       const std::string& /*params*/) override {
        if (method == "Ping") return "pong";
        if (method == "Stats") return R"({"status":"ok"})";
        throw std::runtime_error("unknown method: " + method);
    }
};

int main() {
    std::cout << "=== Integration Tests ===\n";

    const uint16_t TEST_PORT = 19998;

    // ── Test 1: Server with ThreadPool ──
    std::cout << "  Server with ThreadPool start/stop ... ";
    {
        rpc::ThreadPool pool(4);
        rpc::RpcServer server;
        server.set_thread_pool(&pool);
        server.register_service(std::make_shared<PingService>());

        std::thread st([&server]() { server.start(TEST_PORT); });
        std::this_thread::sleep_for(200ms);

        if (server.is_running()) ok(); else fail("server not running");

        server.stop();
        if (st.joinable()) st.join();
    }

    // ── Test 2: Basic Ping call ──
    std::cout << "  PingService::Ping ... ";
    {
        rpc::ThreadPool pool(4);
        rpc::RpcServer server;
        server.set_thread_pool(&pool);
        server.register_service(std::make_shared<PingService>());

        std::thread st([&server]() { server.start(TEST_PORT); });
        std::this_thread::sleep_for(200ms);

        rpc::RpcClient client;
        client.connect("127.0.0.1", TEST_PORT).get();

        auto resp = client.call("PingService", "Ping", "").get();
        if (resp.error_code == 0 && resp.result == "pong") ok();
        else fail("error_code=" + std::to_string(resp.error_code) + " result=" + resp.result);

        client.disconnect();
        server.stop();
        if (st.joinable()) st.join();
    }

    // ── Test 3: PingService::Stats ──
    std::cout << "  PingService::Stats ... ";
    {
        rpc::ThreadPool pool(4);
        rpc::RpcServer server;
        server.set_thread_pool(&pool);
        server.register_service(std::make_shared<PingService>());

        std::thread st([&server]() { server.start(TEST_PORT); });
        std::this_thread::sleep_for(200ms);

        rpc::RpcClient client;
        client.connect("127.0.0.1", TEST_PORT).get();

        auto resp = client.call("PingService", "Stats", "").get();
        if (resp.error_code == 0 && resp.result.find("ok") != std::string::npos) ok();
        else fail("error_code=" + std::to_string(resp.error_code) + " result=" + resp.result);

        client.disconnect();
        server.stop();
        if (st.joinable()) st.join();
    }

    // ── Test 4: Service not found ──
    std::cout << "  Service not found error ... ";
    {
        rpc::ThreadPool pool(4);
        rpc::RpcServer server;
        server.set_thread_pool(&pool);
        server.register_service(std::make_shared<PingService>());

        std::thread st([&server]() { server.start(TEST_PORT); });
        std::this_thread::sleep_for(200ms);

        rpc::RpcClient client;
        client.connect("127.0.0.1", TEST_PORT).get();

        auto resp = client.call("NoSuchService", "Foo", "").get();
        if (resp.error_code != 0 && resp.error_msg.find("service not found") != std::string::npos) ok();
        else fail("expected service not found, got: " + resp.error_msg);

        client.disconnect();
        server.stop();
        if (st.joinable()) st.join();
    }

    // ── Test 5: Method not found ──
    std::cout << "  Method not found error ... ";
    {
        rpc::ThreadPool pool(4);
        rpc::RpcServer server;
        server.set_thread_pool(&pool);
        server.register_service(std::make_shared<PingService>());

        std::thread st([&server]() { server.start(TEST_PORT); });
        std::this_thread::sleep_for(200ms);

        rpc::RpcClient client;
        client.connect("127.0.0.1", TEST_PORT).get();

        auto resp = client.call("PingService", "NoSuchMethod", "").get();
        if (resp.error_code != 0 && resp.error_msg.find("method not found") != std::string::npos) ok();
        else fail("expected method not found, got: " + resp.error_msg);

        client.disconnect();
        server.stop();
        if (st.joinable()) st.join();
    }

    // ── Test 6: Sequential calls (50) ──
    std::cout << "  50 sequential Ping calls ... ";
    {
        rpc::ThreadPool pool(4);
        rpc::RpcServer server;
        server.set_thread_pool(&pool);
        server.register_service(std::make_shared<PingService>());

        std::thread st([&server]() { server.start(TEST_PORT); });
        std::this_thread::sleep_for(200ms);

        rpc::RpcClient client;
        client.connect("127.0.0.1", TEST_PORT).get();

        bool all_ok = true;
        for (int i = 0; i < 50; i++) {
            auto resp = client.call("PingService", "Ping", "msg" + std::to_string(i)).get();
            if (resp.error_code != 0 || resp.result != "pong") {
                all_ok = false;
                break;
            }
        }
        if (all_ok) ok(); else fail("sequential calls failed");

        client.disconnect();
        server.stop();
        if (st.joinable()) st.join();
    }

    // ── Test 7: Concurrent calls from multiple threads ──
    std::cout << "  Concurrent calls (4 threads x 25) ... ";
    {
        rpc::ThreadPool pool(4);
        rpc::RpcServer server;
        server.set_thread_pool(&pool);
        server.register_service(std::make_shared<PingService>());

        std::thread st([&server]() { server.start(TEST_PORT); });
        std::this_thread::sleep_for(200ms);

        rpc::RpcClient client;
        client.connect("127.0.0.1", TEST_PORT).get();

        std::atomic<int> success{0};
        std::atomic<int> fail_count{0};
        std::vector<std::thread> threads;

        for (int t = 0; t < 4; t++) {
            threads.emplace_back([&client, &success, &fail_count, t]() {
                for (int i = 0; i < 25; i++) {
                    auto resp = client.call("PingService", "Ping",
                        "t" + std::to_string(t) + "_" + std::to_string(i)).get();
                    if (resp.error_code == 0 && resp.result == "pong")
                        success++;
                    else
                        fail_count++;
                }
            });
        }

        for (auto& t : threads) t.join();

        if (success == 100 && fail_count == 0) ok();
        else fail("success=" + std::to_string(success) + " fail=" + std::to_string(fail_count));

        client.disconnect();
        server.stop();
        if (st.joinable()) st.join();
    }

    // ── Test 8: Client disconnect/reconnect ──
    std::cout << "  Client disconnect/reconnect ... ";
    {
        rpc::ThreadPool pool(4);
        rpc::RpcServer server;
        server.set_thread_pool(&pool);
        server.register_service(std::make_shared<PingService>());

        std::thread st([&server]() { server.start(TEST_PORT); });
        std::this_thread::sleep_for(200ms);

        // First connection
        {
            rpc::RpcClient client1;
            client1.connect("127.0.0.1", TEST_PORT).get();
            auto resp = client1.call("PingService", "Ping", "first").get();
            if (resp.error_code != 0 || resp.result != "pong") {
                fail("first connection");
                goto disconnect_done;
            }
            client1.disconnect();
            std::this_thread::sleep_for(50ms);
        }

        // Second connection (new client object)
        {
            rpc::RpcClient client2;
            client2.connect("127.0.0.1", TEST_PORT).get();
            auto resp = client2.call("PingService", "Ping", "second").get();
            if (resp.error_code == 0 && resp.result == "pong") ok();
            else fail("second connection: " + resp.error_msg);
            client2.disconnect();
        }

        disconnect_done:
        server.stop();
        if (st.joinable()) st.join();
    }

    // ── Test 9: Large payload round-trip ──
    std::cout << "  Large payload (100KB) ... ";
    {
        rpc::ThreadPool pool(4);
        rpc::RpcServer server;
        server.set_thread_pool(&pool);
        server.register_service(std::make_shared<PingService>());

        std::thread st([&server]() { server.start(TEST_PORT); });
        std::this_thread::sleep_for(200ms);

        rpc::RpcClient client;
        client.connect("127.0.0.1", TEST_PORT).get();

        std::string big(100000, 'Z');
        auto resp = client.call("PingService", "Ping", big).get();
        if (resp.error_code == 0 && resp.result == "pong") ok();
        else fail("large payload failed");

        client.disconnect();
        server.stop();
        if (st.joinable()) st.join();
    }

    // ── Test 10: Server without thread pool (sync mode) ──
    std::cout << "  Server sync mode (no thread pool) ... ";
    {
        rpc::RpcServer server;
        server.register_service(std::make_shared<PingService>());

        std::thread st([&server]() { server.start(TEST_PORT); });
        std::this_thread::sleep_for(200ms);

        rpc::RpcClient client;
        client.connect("127.0.0.1", TEST_PORT).get();

        auto resp = client.call("PingService", "Ping", "sync").get();
        if (resp.error_code == 0 && resp.result == "pong") ok();
        else fail("sync mode: " + resp.error_msg);

        client.disconnect();
        server.stop();
        if (st.joinable()) st.join();
    }

    // ── summary ──
    std::cout << "\n=========================\n";
    std::cout << "Passed: " << passed << "\n";
    std::cout << "Failed: " << failed << "\n";

    return failed > 0 ? 1 : 0;
}
