#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <shared_mutex>
#include <unordered_map>

namespace rpc {

// ── 服务基类 ──
// 每个 RPC 服务继承此类，实现具体的业务逻辑
class Service {
public:
    virtual ~Service() = default;

    // 服务名称（全局唯一）
    virtual std::string name() const = 0;

    // 服务提供的方法列表
    virtual std::vector<std::string> methods() const = 0;

    // 处理远程方法调用
    // method: 方法名
    // params: 序列化后的参数（格式由配置的序列化器决定）
    // 返回: 序列化后的结果
    virtual std::string handle(const std::string& method,
                               const std::string& params) = 0;
};

// ── 服务管理器（单例） ──
// 管理所有注册的 RPC 服务，提供服务注册、查找、调用分发
class ServiceManager {
public:
    // 获取单例实例
    static ServiceManager& instance();

    // 注册服务（同名服务会覆盖旧服务）
    void register_service(std::shared_ptr<Service> service);

    // 注销服务
    void unregister_service(const std::string& name);

    // 查找服务
    std::shared_ptr<Service> get_service(const std::string& name);

    // 检查服务是否存在
    bool has_service(const std::string& name);

    // 列出所有已注册服务名称
    std::vector<std::string> list_services();

    // 分发方法调用到对应服务
    // service_name: 目标服务名
    // method_name:  目标方法名
    // params:       序列化后的参数
    // 返回: 序列化后的结果
    // 异常: 服务/方法不存在时抛 std::runtime_error
    std::string dispatch(const std::string& service_name,
                         const std::string& method_name,
                         const std::string& params);

private:
    ServiceManager() = default;
    ServiceManager(const ServiceManager&) = delete;
    ServiceManager& operator=(const ServiceManager&) = delete;

    std::unordered_map<std::string, std::shared_ptr<Service>> services_;
    mutable std::shared_mutex mutex_;
};

} // namespace rpc
