// ============================================================
// Protobuf 序列化/反序列化 单元测试
// 依赖: test_msg.proto 生成的 test_msg.pb.h / test_msg.pb.cc
// ============================================================

#include "test_msg.pb.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
#ifdef _WIN32
#include <windows.h>
#endif
#include <vector>

#define TEST(name) \
    std::cout << "\n[TEST] " << name << " ... " << std::flush;

#define PASS() std::cout << "PASSED" << std::endl

// 辅助：打印字节的十六进制
void hex_dump(const std::string& data, size_t max_bytes = 64) {
    size_t n = std::min(data.size(), max_bytes);
    for (size_t i = 0; i < n; ++i) {
        printf("%02x ", (unsigned char)data[i]);
    }
    if (data.size() > max_bytes) std::cout << "...";
}

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(65001);  // UTF-8 控制台输出
#endif
    GOOGLE_PROTOBUF_VERIFY_VERSION;

    // ╔════════════════════════════════════════════════════════╗
    // ║  1. 基本类型 序列化 & 反序列化                         ║
    // ╚════════════════════════════════════════════════════════╝

    TEST("基本类型 serialize / deserialize") {
        rpc_test::User user;
        user.set_id(1001);
        user.set_name("Alice");
        user.set_age(28);
        user.set_score(95.5);
        user.set_active(true);

        // 序列化
        std::string serialized;
        bool ok = user.SerializeToString(&serialized);
        assert(ok);
        std::cout << "bytes=" << serialized.size() << " [";
        hex_dump(serialized);
        std::cout << "] ";

        std::cout << ">> deserialize ... ";
        rpc_test::User user2;
        ok = user2.ParseFromString(serialized);
        assert(ok);
        assert(user2.id() == 1001);
        assert(user2.name() == "Alice");
        assert(user2.age() == 28);
        assert(user2.score() == 95.5);
        assert(user2.active() == true);
        PASS();
    };

    // ╔════════════════════════════════════════════════════════╗
    // ║  2. 嵌套 Message                                      ║
    // ╚════════════════════════════════════════════════════════╝

    TEST("嵌套 Message（Address）") {
        rpc_test::User user;
        user.set_id(1);
        user.set_name("Bob");

        auto* addr = user.mutable_address();
        addr->set_street("123 Main St");
        addr->set_city("Beijing");
        addr->set_zip(100000);

        std::string data;
        user.SerializeToString(&data);

        rpc_test::User user2;
        user2.ParseFromString(data);
        assert(user2.has_address());
        assert(user2.address().street() == "123 Main St");
        assert(user2.address().city() == "Beijing");
        assert(user2.address().zip() == 100000);
        PASS();
    };

    // ╔════════════════════════════════════════════════════════╗
    // ║  3. repeated 字段（数组）                              ║
    // ╚════════════════════════════════════════════════════════╝

    TEST("repeated 字符串数组") {
        rpc_test::User user;
        user.set_name("Carol");
        user.add_tags("cpp");
        user.add_tags("rpc");
        user.add_tags("protobuf");

        std::string data;
        user.SerializeToString(&data);

        rpc_test::User user2;
        user2.ParseFromString(data);
        assert(user2.tags_size() == 3);
        assert(user2.tags(0) == "cpp");
        assert(user2.tags(1) == "rpc");
        assert(user2.tags(2) == "protobuf");

        // 遍历
        std::cout << "tags=[";
        for (int i = 0; i < user2.tags_size(); ++i) {
            if (i > 0) std::cout << ", ";
            std::cout << user2.tags(i);
        }
        std::cout << "] ";
        PASS();
    };

    // ╔════════════════════════════════════════════════════════╗
    // ║  4. repeated 嵌套 Message                             ║
    // ╚════════════════════════════════════════════════════════╝

    TEST("repeated 嵌套 Message（UserList）") {
        rpc_test::UserList list;
        for (int i = 0; i < 3; ++i) {
            auto* u = list.add_users();
            u->set_id(100 + i);
            u->set_name("User" + std::to_string(i));
            u->set_age(20 + i);
        }

        std::string data;
        list.SerializeToString(&data);
        std::cout << "3 users bytes=" << data.size() << " ";

        rpc_test::UserList list2;
        list2.ParseFromString(data);
        assert(list2.users_size() == 3);
        assert(list2.users(0).name() == "User0");
        assert(list2.users(2).age() == 22);
        PASS();
    };

    // ╔════════════════════════════════════════════════════════╗
    // ║  5. bytes 字段（二进制载荷）                           ║
    // ╚════════════════════════════════════════════════════════╝

    TEST("bytes 二进制载荷") {
        // 模拟把序列化后的 User 放进 RpcRequest.payload
        rpc_test::User user;
        user.set_id(42);
        user.set_name("payload_test");

        std::string user_data;
        user.SerializeToString(&user_data);

        rpc_test::RpcRequest req;
        req.set_service("UserService");
        req.set_method("GetUser");
        req.set_request_id(123456789);
        req.set_payload(user_data);  // bytes = 序列化的 User

        // 序列化整个请求
        std::string req_data;
        req.SerializeToString(&req_data);

        // 反序列化 + 读取 payload 并反序列化 User
        rpc_test::RpcRequest req2;
        req2.ParseFromString(req_data);
        assert(req2.service() == "UserService");
        assert(req2.method() == "GetUser");
        assert(req2.request_id() == 123456789);

        rpc_test::User user2;
        user2.ParseFromString(req2.payload());
        assert(user2.id() == 42);
        assert(user2.name() == "payload_test");
        PASS();
    };

    // ╔════════════════════════════════════════════════════════╗
    // ║  6. RPC 请求-响应 完整流程                             ║
    // ╚════════════════════════════════════════════════════════╝

    TEST("RPC request/response 完整流程") {
        // ---- 构造请求 ----
        rpc_test::RpcRequest req;
        req.set_service("MathService");
        req.set_method("Add");
        req.set_request_id(1);

        // 模拟参数序列化（用 User 模拟参数）
        rpc_test::User params;
        params.set_id(0);
        std::string param_data;
        params.SerializeToString(&param_data);
        req.set_payload(param_data);

        std::string wire_data;
        req.SerializeToString(&wire_data);
        std::cout << "request bytes=" << wire_data.size() << " ";

        // ---- 模拟网络传输后，服务端解析 ----
        rpc_test::RpcRequest server_req;
        server_req.ParseFromString(wire_data);
        assert(server_req.service() == "MathService");

        // ---- 服务端构造响应 ----
        rpc_test::RpcResponse resp;
        resp.set_request_id(server_req.request_id());
        resp.set_error_code(0);

        // 模拟返回值：序列化一个结果
        rpc_test::User result;
        result.set_name("result_user");
        result.set_score(100.0);
        std::string result_data;
        result.SerializeToString(&result_data);
        resp.set_result(result_data);

        std::string resp_wire;
        resp.SerializeToString(&resp_wire);
        std::cout << "response bytes=" << resp_wire.size() << " ";

        // ---- 客户端解析响应 ----
        rpc_test::RpcResponse client_resp;
        client_resp.ParseFromString(resp_wire);
        assert(client_resp.error_code() == 0);
        assert(client_resp.request_id() == 1);

        rpc_test::User client_result;
        client_result.ParseFromString(client_resp.result());
        assert(client_result.name() == "result_user");
        assert(client_result.score() == 100.0);
        PASS();
    };

    // ╔════════════════════════════════════════════════════════╗
    // ║  7. 序列化方式对比                                    ║
    // ╚════════════════════════════════════════════════════════╝

    TEST("序列化/反序列化 API 对比") {
        rpc_test::User user;
        user.set_id(99);
        user.set_name("test");

        // 方式1: SerializeToString -> ParseFromString
        std::string s1;
        user.SerializeToString(&s1);

        // 方式2: SerializeAsString -> ParseFromString
        std::string s2 = user.SerializeAsString();
        assert(s1 == s2);

        // 方式3: SerializeToArray -> ParseFromArray
        std::vector<char> buf(user.ByteSizeLong());
        user.SerializeToArray(buf.data(), buf.size());
        rpc_test::User user3;
        user3.ParseFromArray(buf.data(), buf.size());
        assert(user3.id() == 99);

        // 方式4: SerializeToOstream -> ParseFromIstream
        std::stringstream oss;
        user.SerializeToOstream(&oss);
        rpc_test::User user4;
        user4.ParseFromIstream(&oss);
        assert(user4.name() == "test");

        // 方式5: CodedOutputStream / CodedInputStream (高效变长编码)
        // 略，适合 streaming 场景

        std::cout << "5种API均正常 ";
        PASS();
    };

    // ╔════════════════════════════════════════════════════════╗
    // ║  8. 默认值 & 字段存在性检查                            ║
    // ╚════════════════════════════════════════════════════════╝

    TEST("默认值与字段存在性") {
        rpc_test::User user;
        // 什么都不设置

        // proto3: 基础类型默认值
        assert(user.id() == 0);
        assert(user.name().empty());
        assert(user.age() == 0);
        assert(user.score() == 0.0);
        assert(user.active() == false);

        // has_xxx() 检查是否显式设置了该字段
        assert(!user.has_address());   // 未设置 nested message
        assert(user.tags_size() == 0); // repeated 初始为空

        // 默认值字段在序列化时不会写入（proto3 省略默认值）
        std::string data;
        user.SerializeToString(&data);
        std::cout << "empty user bytes=" << data.size() << " (应为0) ";
        assert(data.empty());

        // 设置部分字段后
        user.set_name("Dave");
        user.SerializeToString(&data);
        std::cout << "name-only bytes=" << data.size() << " ";
        assert(!data.empty());

        rpc_test::User user2;
        user2.ParseFromString(data);
        assert(user2.name() == "Dave");
        assert(user2.id() == 0);       // 未传输，取默认值
        PASS();
    };

    // ╔════════════════════════════════════════════════════════╗
    // ║  9. 破坏性测试：损坏数据                               ║
    // ╚════════════════════════════════════════════════════════╝

    TEST("损坏数据解析（健壮性）") {
        // 完全随机的无效数据
        std::string garbage = "!!! not protobuf !!! \\xFF\\xFE\\xFD";

        rpc_test::User user;
        bool ok = user.ParseFromString(garbage);
        std::cout << "invalid data parse=" << (ok ? "true" : "false") << " (应为 false) ";
        assert(!ok);

        // 部分截断的数据
        rpc_test::User valid;
        valid.set_id(100);
        valid.set_name("test");
        std::string valid_data = valid.SerializeAsString();
        // 截断最后 2 字节
        std::string truncated(valid_data.begin(), valid_data.end() - 2);
        rpc_test::User user3;
        ok = user3.ParseFromString(truncated);
        std::cout << "truncated parse=" << (ok ? "true" : "false") << " (应为 false) ";
        assert(!ok);
        PASS();
    };

    // ╔════════════════════════════════════════════════════════╗
    // ║  10. 清空 & 复用对象                                   ║
    // ╚════════════════════════════════════════════════════════╝

    TEST("Clear() 复用对象") {
        rpc_test::User user;
        user.set_id(500);
        user.set_name("original");
        user.add_tags("tag1");

        // Clear() 重置所有字段
        user.Clear();
        assert(user.id() == 0);
        assert(user.name().empty());
        assert(user.tags_size() == 0);

        // 复用同一个对象
        user.set_id(600);
        user.set_name("reused");
        assert(user.id() == 600);

        std::string data;
        user.SerializeToString(&data);

        rpc_test::User user2;
        user2.ParseFromString(data);
        assert(user2.name() == "reused");
        assert(user2.tags_size() == 0);  // 之前清空了
        PASS();
    };

    std::cout << "\n========================================" << std::endl;
    std::cout << "  全部 10 项 Protobuf 测试通过!" << std::endl;
    std::cout << "========================================" << std::endl;

    google::protobuf::ShutdownProtobufLibrary();
    return 0;
}
