#include <iostream>
#include <string>
#include <memory>

#include "serializer/serializer.h"
#include "protocol/rpc_protocol.h"
#include "service/service_manager.h"
#include "protos/message_proto.pb.h"

static int passed = 0;
static int failed = 0;

void ok() { std::cout << "OK\n"; passed++; }
void fail(const std::string& msg) { std::cout << "FAIL: " << msg << "\n"; failed++; }

int main() {
    std::cout << "=== Serializer Tests ===\n";

    // ── Serializer factory ──
    std::cout << "  create protobuf serializer by enum ... ";
    {
        auto s = rpc::create_serializer(rpc::SerializerType::PROTOBUF);
        if (s->name() == "protobuf") ok(); else fail("wrong name: " + s->name());
    }

    std::cout << "  create json serializer by enum ... ";
    {
        auto s = rpc::create_serializer(rpc::SerializerType::JSON);
        if (s->name() == "json") ok(); else fail("wrong name: " + s->name());
    }

    std::cout << "  create serializer by string ... ";
    {
        auto s1 = rpc::create_serializer("protobuf");
        auto s2 = rpc::create_serializer("proto");
        auto s3 = rpc::create_serializer("json");
        if (s1->name() == "protobuf" && s2->name() == "protobuf" && s3->name() == "json")
            ok(); else fail("name mismatch");
    }

    std::cout << "  unknown serializer throws ... ";
    {
        try {
            rpc::create_serializer("yaml");
            fail("expected exception");
        } catch (const std::runtime_error&) {
            ok();
        }
    }

    // ── Protobuf serializer roundtrip ──
    std::cout << "  protobuf serialize/deserialize ... ";
    {
        test_msg::User user;
        user.set_id(42);
        user.set_name("Alice");
        user.set_email("alice@example.com");

        auto s = rpc::create_serializer(rpc::SerializerType::PROTOBUF);
        std::string data = s->serialize(user);

        test_msg::User decoded;
        bool ok_flag = s->deserialize(data, decoded);
        if (ok_flag && decoded.id() == 42 && decoded.name() == "Alice"
            && decoded.email() == "alice@example.com")
            ok(); else fail("data mismatch");
    }

    std::cout << "  protobuf deserialize garbage ... ";
    {
        test_msg::User user;
        auto s = rpc::create_serializer(rpc::SerializerType::PROTOBUF);
        if (s->deserialize("!!!garbage!!!", user))
            fail("expected false");
        else
            ok();
    }

    // ── JSON serializer roundtrip ──
    std::cout << "  json serialize/deserialize ... ";
    {
        test_msg::User user;
        user.set_id(7);
        user.set_name("Bob");
        user.set_email("bob@test.org");

        auto s = rpc::create_serializer(rpc::SerializerType::JSON);
        std::string json = s->serialize(user);

        // verify it's JSON
        if (json.find("\"id\"") != std::string::npos &&
            json.find("\"name\"") != std::string::npos) {

            test_msg::User decoded;
            bool ok_flag = s->deserialize(json, decoded);
            if (ok_flag && decoded.id() == 7 && decoded.name() == "Bob"
                && decoded.email() == "bob@test.org")
                ok(); else fail("data mismatch");
        } else {
            fail("not valid JSON: " + json);
        }
    }

    std::cout << "  json deserialize bad input ... ";
    {
        test_msg::User user;
        auto s = rpc::create_serializer(rpc::SerializerType::JSON);
        if (s->deserialize("{bad json}", user))
            fail("expected false");
        else
            ok();
    }

    std::cout << "  both serializers produce different output ... ";
    {
        test_msg::User user;
        user.set_id(1);
        user.set_name("Same");

        auto pb = rpc::create_serializer(rpc::SerializerType::PROTOBUF);
        auto js = rpc::create_serializer(rpc::SerializerType::JSON);
        std::string pb_data = pb->serialize(user);
        std::string js_data = js->serialize(user);

        if (pb_data != js_data) ok(); else fail("outputs should differ");
    }

    // ── SerializerType roundtrip ──
    std::cout << "  SerializerType matches name ... ";
    {
        auto pb = rpc::create_serializer(rpc::SerializerType::PROTOBUF);
        auto js = rpc::create_serializer(rpc::SerializerType::JSON);
        if (pb->type() == rpc::SerializerType::PROTOBUF &&
            js->type() == rpc::SerializerType::JSON)
            ok(); else fail("type mismatch");
    }

    // ── Nested message serialization ──
    std::cout << "  protobuf nested message ... ";
    {
        test_msg::GetUserResponse resp;
        resp.set_error_code(0);
        resp.set_error_msg("OK");
        resp.mutable_user()->set_id(99);
        resp.mutable_user()->set_name("Charlie");

        auto s = rpc::create_serializer(rpc::SerializerType::PROTOBUF);
        std::string data = s->serialize(resp);

        test_msg::GetUserResponse decoded;
        if (s->deserialize(data, decoded) && decoded.error_code() == 0
            && decoded.user().id() == 99 && decoded.user().name() == "Charlie")
            ok(); else fail("nested message mismatch");
    }

    std::cout << "\n=== RPC Protocol Tests ===\n";

    // ── Header serialization ──
    std::cout << "  header serialize/deserialize ... ";
    {
        rpc::RpcHeader hdr{};
        hdr.magic      = rpc::RpcHeader::MAGIC;
        hdr.version    = rpc::RpcHeader::VERSION;
        hdr.msg_type   = static_cast<uint8_t>(rpc::MsgType::REQUEST);
        hdr.serializer = static_cast<uint8_t>(rpc::SerType::PROTOBUF);
        hdr.flags      = rpc::FLAG_COMPRESS;
        hdr.seq_id     = 12345;
        hdr.body_size  = 512;
        hdr.checksum   = 0xDEADBEEF;

        std::string wire = rpc::serialize_header(hdr);
        if (wire.size() != 20) {
            fail("header size wrong: " + std::to_string(wire.size()));
            goto next_hdr;
        }

        auto hdr2 = rpc::deserialize_header(wire);
        if (hdr2.magic == hdr.magic && hdr2.version == hdr.version &&
            hdr2.msg_type == hdr.msg_type && hdr2.serializer == hdr.serializer &&
            hdr2.flags == hdr.flags && hdr2.seq_id == hdr.seq_id &&
            hdr2.body_size == hdr.body_size && hdr2.checksum == hdr.checksum)
            ok(); else fail("header field mismatch");
    }
    next_hdr:

    std::cout << "  header too short throws ... ";
    {
        try {
            rpc::deserialize_header("short");
            fail("expected exception");
        } catch (const std::runtime_error&) {
            ok();
        }
    }

    std::cout << "  header magic validation ... ";
    {
        rpc::RpcHeader hdr{};
        hdr.magic = rpc::RpcHeader::MAGIC;
        std::string wire = rpc::serialize_header(hdr);
        auto hdr2 = rpc::deserialize_header(wire);
        if (hdr2.magic == rpc::RpcHeader::MAGIC) ok(); else fail("magic mismatch");
    }

    // ── Request serialization ──
    std::cout << "  request serialize/deserialize ... ";
    {
        rpc::RpcRequest req;
        req.service_name = "UserService";
        req.method_name  = "GetUser";
        req.timeout_ms   = 5000;
        req.params       = "binary_params_data";

        std::string wire = req.serialize();
        auto req2 = rpc::RpcRequest::deserialize(wire);

        if (req2.service_name == "UserService" &&
            req2.method_name == "GetUser" &&
            req2.timeout_ms == 5000 &&
            req2.params == "binary_params_data")
            ok(); else fail("request field mismatch");
    }

    std::cout << "  request empty fields ... ";
    {
        rpc::RpcRequest req;
        req.service_name = "";
        req.method_name  = "";
        req.timeout_ms   = 0;
        req.params       = "";

        std::string wire = req.serialize();
        auto req2 = rpc::RpcRequest::deserialize(wire);

        if (req2.service_name.empty() && req2.method_name.empty() &&
            req2.timeout_ms == 0 && req2.params.empty())
            ok(); else fail("empty field mismatch");
    }

    std::cout << "  request with binary params ... ";
    {
        rpc::RpcRequest req;
        req.service_name = "Svc";
        req.method_name  = "Method";
        req.timeout_ms   = 1000;
        req.params       = std::string("\x00\x01\x02\xFF\xFE\xFD", 6);

        std::string wire = req.serialize();
        auto req2 = rpc::RpcRequest::deserialize(wire);

        if (req2.params.size() == 6 && req2.params == req.params)
            ok(); else fail("binary params mismatch");
    }

    // ── Response serialization ──
    std::cout << "  response serialize/deserialize (success) ... ";
    {
        rpc::RpcResponse resp;
        resp.error_code = 0;
        resp.error_msg  = "";
        resp.result     = "serialized_user_data";

        std::string wire = resp.serialize();
        auto resp2 = rpc::RpcResponse::deserialize(wire);

        if (resp2.error_code == 0 && resp2.error_msg.empty() &&
            resp2.result == "serialized_user_data")
            ok(); else fail("response field mismatch");
    }

    std::cout << "  response serialize/deserialize (error) ... ";
    {
        rpc::RpcResponse resp;
        resp.error_code = -1;
        resp.error_msg  = "Service not found";
        resp.result     = "";

        std::string wire = resp.serialize();
        auto resp2 = rpc::RpcResponse::deserialize(wire);

        if (resp2.error_code == -1 && resp2.error_msg == "Service not found" &&
            resp2.result.empty())
            ok(); else fail("error response mismatch");
    }

    std::cout << "  response deserialize truncated ... ";
    {
        try {
            rpc::RpcResponse::deserialize("ab");
            fail("expected exception");
        } catch (const std::runtime_error&) {
            ok();
        }
    }

    // ── CRC32 ──
    std::cout << "  crc32 deterministic ... ";
    {
        uint32_t c1 = rpc::crc32_checksum("hello");
        uint32_t c2 = rpc::crc32_checksum("hello");
        if (c1 == c2) ok(); else fail("CRC32 not deterministic");
    }

    std::cout << "  crc32 different for different data ... ";
    {
        uint32_t c1 = rpc::crc32_checksum("hello");
        uint32_t c2 = rpc::crc32_checksum("world");
        if (c1 != c2) ok(); else fail("CRC32 should differ");
    }

    std::cout << "  crc32 empty string ... ";
    {
        uint32_t c = rpc::crc32_checksum("");
        if (c == 0) ok(); else fail("CRC32 empty should be 0");
    }

    // ── Message type flags ──
    std::cout << "  MsgType values ... ";
    {
        if (static_cast<uint8_t>(rpc::MsgType::REQUEST) == 0x01 &&
            static_cast<uint8_t>(rpc::MsgType::RESPONSE) == 0x02 &&
            static_cast<uint8_t>(rpc::MsgType::RPC_ERROR) == 0x03 &&
            static_cast<uint8_t>(rpc::MsgType::HEARTBEAT) == 0x04)
            ok(); else fail("MsgType value mismatch");
    }

    std::cout << "  RpcFlags values ... ";
    {
        if (rpc::FLAG_COMPRESS == 0x01 && rpc::FLAG_ENCRYPT == 0x02)
            ok(); else fail("flag value mismatch");
    }

    std::cout << "\n=== Service Manager Tests ===\n";

    // ── Service implementation for testing ──
    class MockEchoService : public rpc::Service {
    public:
        std::string name() const override { return "EchoService"; }
        std::vector<std::string> methods() const override {
            return {"Echo", "Greet"};
        }
        std::string handle(const std::string& method,
                          const std::string& params) override {
            if (method == "Echo") return params;
            if (method == "Greet") return "Hello, " + params + "!";
            throw std::runtime_error("unknown method: " + method);
        }
    };

    class MockCalcService : public rpc::Service {
    public:
        std::string name() const override { return "CalcService"; }
        std::vector<std::string> methods() const override {
            return {"Add", "Sub"};
        }
        std::string handle(const std::string& method,
                          const std::string& /*params*/) override {
            if (method == "Add") return "add_result";
            if (method == "Sub") return "sub_result";
            throw std::runtime_error("unknown method: " + method);
        }
    };

    auto& mgr = rpc::ServiceManager::instance();

    std::cout << "  register service ... ";
    {
        auto svc = std::make_shared<MockEchoService>();
        mgr.register_service(svc);
        if (mgr.has_service("EchoService")) ok(); else fail("not found after register");
    }

    std::cout << "  get service ... ";
    {
        auto svc = mgr.get_service("EchoService");
        if (svc && svc->name() == "EchoService") ok(); else fail("wrong service");
    }

    std::cout << "  get non-existent service returns null ... ";
    {
        auto svc = mgr.get_service("NonExistent");
        if (!svc) ok(); else fail("should be null");
    }

    std::cout << "  has_service ... ";
    {
        if (mgr.has_service("EchoService") && !mgr.has_service("FakeService"))
            ok(); else fail("has_service wrong");
    }

    std::cout << "  list services ... ";
    {
        auto svc2 = std::make_shared<MockCalcService>();
        mgr.register_service(svc2);

        auto list = mgr.list_services();
        if (list.size() == 2) {
            // should be sorted alphabetically
            if (list[0] == "CalcService" && list[1] == "EchoService")
                ok(); else fail("order wrong");
        } else {
            fail("expected 2, got " + std::to_string(list.size()));
        }
    }

    std::cout << "  dispatch Echo ... ";
    {
        std::string result = mgr.dispatch("EchoService", "Echo", "test_msg");
        if (result == "test_msg") ok(); else fail("Echo returned: " + result);
    }

    std::cout << "  dispatch Greet ... ";
    {
        std::string result = mgr.dispatch("EchoService", "Greet", "World");
        if (result == "Hello, World!") ok(); else fail("Greet returned: " + result);
    }

    std::cout << "  dispatch non-existent service ... ";
    {
        try {
            mgr.dispatch("NoService", "Foo", "");
            fail("expected exception");
        } catch (const std::runtime_error& e) {
            if (std::string(e.what()).find("service not found") != std::string::npos)
                ok(); else fail("wrong error: " + std::string(e.what()));
        }
    }

    std::cout << "  dispatch non-existent method ... ";
    {
        try {
            mgr.dispatch("EchoService", "NoMethod", "");
            fail("expected exception");
        } catch (const std::runtime_error& e) {
            if (std::string(e.what()).find("method not found") != std::string::npos)
                ok(); else fail("wrong error: " + std::string(e.what()));
        }
    }

    std::cout << "  unregister service ... ";
    {
        mgr.unregister_service("CalcService");
        if (!mgr.has_service("CalcService") && mgr.has_service("EchoService"))
            ok(); else fail("unregister failed");
    }

    std::cout << "  re-register updates ... ";
    {
        auto svc1 = std::make_shared<MockEchoService>();
        mgr.register_service(svc1);  // overwrite
        if (mgr.list_services().size() == 1) ok(); else fail("should still be 1");
    }

    // Clean up for future tests
    mgr.unregister_service("EchoService");

    // ── summary ──
    std::cout << "\n=========================\n";
    std::cout << "Passed: " << passed << "\n";
    std::cout << "Failed: " << failed << "\n";

    return failed > 0 ? 1 : 0;
}
