#include "service_manager.h"

#include <mutex>
#include <stdexcept>
#include <algorithm>

namespace rpc {

ServiceManager& ServiceManager::instance() {
    static ServiceManager mgr;
    return mgr;
}

void ServiceManager::register_service(std::shared_ptr<Service> service) {
    if (!service) return;
    std::unique_lock lock(mutex_);
    services_[service->name()] = std::move(service);
}

void ServiceManager::unregister_service(const std::string& name) {
    std::unique_lock lock(mutex_);
    services_.erase(name);
}

std::shared_ptr<Service> ServiceManager::get_service(const std::string& name) {
    std::shared_lock lock(mutex_);
    auto it = services_.find(name);
    if (it == services_.end())
        return nullptr;
    return it->second;
}

bool ServiceManager::has_service(const std::string& name) {
    std::shared_lock lock(mutex_);
    return services_.find(name) != services_.end();
}

std::vector<std::string> ServiceManager::list_services() {
    std::shared_lock lock(mutex_);
    std::vector<std::string> names;
    names.reserve(services_.size());
    for (const auto& [name, _] : services_) {
        names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

std::string ServiceManager::dispatch(const std::string& service_name,
                                     const std::string& method_name,
                                     const std::string& params) {
    auto svc = get_service(service_name);
    if (!svc) {
        throw std::runtime_error(
            "service not found: " + service_name);
    }

    // 验证方法是否存在
    auto methods = svc->methods();
    if (std::find(methods.begin(), methods.end(), method_name) == methods.end()) {
        throw std::runtime_error(
            "method not found: " + service_name + "." + method_name);
    }

    return svc->handle(method_name, params);
}

} // namespace rpc
