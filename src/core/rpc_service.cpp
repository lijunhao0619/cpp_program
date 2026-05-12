#include "rpc_service.h"

#include <iostream>
#include <thread_pool/thread_pool.h>

namespace rpc {

RpcServer::~RpcServer() {
    stop();
}

void RpcServer::set_thread_pool(ThreadPool* pool) {
    thread_pool_ = pool;
}

void RpcServer::register_service(std::shared_ptr<Service> service) {
    ServiceManager::instance().register_service(std::move(service));
}

void RpcServer::start(uint16_t port) {
    running_ = true;

    cycle_.start_listen(port,
        [this](const RpcHeader& header, std::string body,
               std::shared_ptr<Connection> conn) {
            on_message(header, std::move(body), std::move(conn));
        });

    std::cout << "RPC server listening on port " << port << std::endl;
    cycle_.run();
}

void RpcServer::stop() {
    running_ = false;
    cycle_.stop();
}

bool RpcServer::is_running() const {
    return running_.load();
}

size_t RpcServer::connection_count() {
    return cycle_.connections().count();
}

void RpcServer::on_message(const RpcHeader& header, std::string body,
                           std::shared_ptr<Connection> conn) {

    if (thread_pool_) {
        // 提交到线程池并发处理
        thread_pool_->enqueue([this, header, body = std::move(body), conn]() {
            auto resp = process_request(header, body);
            send_response(header, resp, conn);
        });
    } else {
        // 直接在 io_context 线程中同步处理
        auto resp = process_request(header, body);
        send_response(header, resp, conn);
    }
}

RpcResponse RpcServer::process_request(const RpcHeader& header,
                                       const std::string& body) {
    RpcResponse resp;

    // 只处理 REQUEST 类型
    if (header.msg_type != static_cast<uint8_t>(MsgType::REQUEST)) {
        resp.error_code = static_cast<int32_t>(ErrorCode::INTERNAL_ERROR);
        resp.error_msg  = "unexpected message type";
        return resp;
    }

    // 反序列化请求体
    RpcRequest req;
    try {
        req = RpcRequest::deserialize(body);
    } catch (const std::exception& e) {
        resp.error_code = static_cast<int32_t>(ErrorCode::DESERIALIZE_ERROR);
        resp.error_msg  = std::string("deserialize failed: ") + e.what();
        return resp;
    }

    // 分发到 ServiceManager
    try {
        std::string result = ServiceManager::instance().dispatch(
            req.service_name, req.method_name, req.params);

        resp.error_code = 0;
        resp.result     = std::move(result);
    } catch (const std::runtime_error& e) {
        std::string err = e.what();
        if (err.find("service not found") != std::string::npos) {
            resp.error_code = static_cast<int32_t>(ErrorCode::SERVICE_NOT_FOUND);
        } else if (err.find("method not found") != std::string::npos) {
            resp.error_code = static_cast<int32_t>(ErrorCode::METHOD_NOT_FOUND);
        } else {
            resp.error_code = static_cast<int32_t>(ErrorCode::INTERNAL_ERROR);
        }
        resp.error_msg = err;
    }

    return resp;
}

void RpcServer::send_response(const RpcHeader& req_header,
                              const RpcResponse& resp,
                              std::shared_ptr<Connection> conn) {
    std::string resp_body = resp.serialize();

    RpcHeader resp_header{};
    resp_header.magic      = RpcHeader::MAGIC;
    resp_header.version    = RpcHeader::VERSION;
    resp_header.msg_type   = (resp.error_code == 0)
        ? static_cast<uint8_t>(MsgType::RESPONSE)
        : static_cast<uint8_t>(MsgType::RPC_ERROR);
    resp_header.serializer = req_header.serializer;
    resp_header.flags      = FLAG_NONE;
    resp_header.seq_id     = req_header.seq_id;
    resp_header.body_size  = static_cast<uint32_t>(resp_body.size());
    resp_header.checksum   = 0;

    conn->send(resp_header, resp_body);
}

} // namespace rpc
