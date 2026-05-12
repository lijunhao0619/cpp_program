#pragma once

#include <memory>
#include <shared_mutex>
#include <unordered_map>
#include <vector>
#include <cstdint>

#include "connection.h"

namespace rpc {

// ── 连接池管理器 ──
// 线程安全地管理所有活跃连接
class ConnectionManager {
public:
    ConnectionManager() = default;

    // 添加连接，返回分配的连接 ID
    uint64_t add(std::shared_ptr<Connection> conn);

    // 移除连接
    void remove(uint64_t conn_id);

    // 获取连接（不存在返回 nullptr）
    std::shared_ptr<Connection> get(uint64_t conn_id);

    // 当前连接数
    size_t count();

    // 获取所有连接
    std::vector<std::shared_ptr<Connection>> all_connections();

    // 关闭所有连接
    void close_all();

private:
    std::unordered_map<uint64_t, std::shared_ptr<Connection>> conns_;
    mutable std::shared_mutex mux_;
    std::atomic<uint64_t> next_id_{1};
};

} // namespace rpc
