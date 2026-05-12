#include "connection_manager.h"

#include <mutex>

namespace rpc {

uint64_t ConnectionManager::add(std::shared_ptr<Connection> conn) {
    uint64_t id = next_id_++;
    conn->set_conn_id(id);
    std::unique_lock lock(mux_);
    conns_[id] = std::move(conn);
    return id;
}

void ConnectionManager::remove(uint64_t conn_id) {
    std::unique_lock lock(mux_);
    conns_.erase(conn_id);
}

std::shared_ptr<Connection> ConnectionManager::get(uint64_t conn_id) {
    std::shared_lock lock(mux_);
    auto it = conns_.find(conn_id);
    if (it == conns_.end()) return nullptr;
    return it->second;
}

size_t ConnectionManager::count() {
    std::shared_lock lock(mux_);
    return conns_.size();
}

std::vector<std::shared_ptr<Connection>> ConnectionManager::all_connections() {
    std::shared_lock lock(mux_);
    std::vector<std::shared_ptr<Connection>> vec;
    vec.reserve(conns_.size());
    for (auto& [id, conn] : conns_) {
        vec.push_back(conn);
    }
    return vec;
}

void ConnectionManager::close_all() {
    // 先取出所有连接，释放锁，再关闭（避免死锁）
    std::vector<std::shared_ptr<Connection>> snapshot;
    {
        std::unique_lock lock(mux_);
        snapshot.reserve(conns_.size());
        for (auto& [id, conn] : conns_) {
            snapshot.push_back(conn);
        }
        conns_.clear();
    }

    for (auto& conn : snapshot) {
        conn->close();
    }
}

} // namespace rpc
